#!/usr/bin/env python3
"""Solar monitor: receives inverter readings pushed by the AhoyDTU (ESP32) and serves a
public dashboard with full history. Standard library only (http.server + sqlite3)."""

import csv
import io
import json
import os
import sqlite3
import threading
import time
from datetime import datetime, timedelta
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlencode, urlparse
from urllib.request import urlopen
from zoneinfo import ZoneInfo

HERE = Path(__file__).resolve().parent
WEB_DIR = HERE / "web"

# site configuration: everything specific to one installation lives in config.json
# (see config.example.json); secrets and paths come from the environment
DEFAULTS = {
    "site_name": "Solar Monitor",
    "place": "",
    "timezone": "UTC",
    "latitude": 0.0,
    "longitude": 0.0,
    "public_location_decimals": 1,
    "inverter": {"model": "Hoymiles inverter", "panels": 4, "panel_max_w": 400},
    "pv": {"tilt": 20, "azimuth": 0, "peak_w": 1600, "performance_ratio": 0.8, "inverter_max_w": 1500},
    "photos": {},
}


def load_config():
    cfg = json.loads(json.dumps(DEFAULTS))
    path = Path(os.environ.get("SOLAR_CONFIG", HERE / "config.json"))
    if not path.is_file():
        path = HERE / "config.example.json"
    if path.is_file():
        for k, v in json.loads(path.read_text()).items():
            if isinstance(v, dict) and isinstance(cfg.get(k), dict):
                cfg[k].update(v)
            else:
                cfg[k] = v
    return cfg


CONFIG = load_config()
DB_PATH = os.environ.get("SOLAR_DB", str(HERE / "solar.db"))
TOKEN = os.environ.get("SOLAR_TOKEN", "")
PORT = int(os.environ.get("SOLAR_PORT", "8742"))
TZ = ZoneInfo(CONFIG["timezone"])

FIELDS = ["pac", "yd", "yt", "temp", "uac", "freq",
          "p1", "p2", "p3", "p4", "u1", "u2", "u3", "u4", "i1", "i2", "i3", "i4"]

_db_lock = threading.Lock()

# forecast: Open-Meteo, irradiance on the panel plane -> expected production.
# The location is rounded before it leaves the server (forecast request and the public config).
_PV = CONFIG["pv"]
_DEC = int(CONFIG["public_location_decimals"])
LAT, LON = round(float(CONFIG["latitude"]), _DEC), round(float(CONFIG["longitude"]), _DEC)
PANEL_TILT, PANEL_AZIMUTH = _PV["tilt"], _PV["azimuth"]
PEAK_W, PERF_RATIO, INVERTER_MAX_W = _PV["peak_w"], _PV["performance_ratio"], _PV["inverter_max_w"]


def public_config():
    """what the web pages need; never includes secrets or the exact location"""
    return {"site_name": CONFIG["site_name"], "place": CONFIG["place"], "timezone": CONFIG["timezone"],
            "latitude": LAT, "longitude": LON, "inverter": CONFIG["inverter"], "photos": CONFIG["photos"]}


_forecast = {"ts": 0, "data": None}
_forecast_lock = threading.Lock()


def db():
    conn = sqlite3.connect(DB_PATH, timeout=10)
    conn.row_factory = sqlite3.Row
    return conn


def init_db():
    with db() as conn:
        conn.execute("PRAGMA journal_mode=WAL")
        cols = ", ".join(f"{f} REAL" for f in FIELDS)
        conn.execute(f"CREATE TABLE IF NOT EXISTS readings (ts INTEGER PRIMARY KEY, {cols})")
        # latest full snapshot of every inverter value, for the live detail page
        conn.execute("CREATE TABLE IF NOT EXISTS detail (id INTEGER PRIMARY KEY CHECK (id = 1), ts INTEGER, json TEXT)")
        # link health reported by the DTU about once a minute (server receive time)
        conn.execute("CREATE TABLE IF NOT EXISTS health (ts INTEGER PRIMARY KEY, uptime INTEGER, rssi INTEGER, reset TEXT, heap INTEGER,"
                     " nrf INTEGER, tx INTEGER, ok INTEGER, fail INTEGER, none INTEGER, last_rx INTEGER, queue INTEGER, fail_streak INTEGER)")


def local_day_bounds(day):
    start = datetime.combine(day, datetime.min.time(), TZ)
    end = start + timedelta(days=1)
    return int(start.timestamp()), int(end.timestamp())


def ingest(payload):
    rows = payload.get("readings") if isinstance(payload, dict) else None
    if not isinstance(rows, list):
        raise ValueError("expected {\"readings\": [...]}")
    now = time.time()
    clean = []
    for r in rows[:500]:
        ts = int(r.get("ts", 0))
        if ts < 1_600_000_000 or ts > now + 300:
            continue  # inverter clock not set or garbage
        clean.append([ts] + [float(r[f]) if isinstance(r.get(f), (int, float)) else None for f in FIELDS])
    detail = payload.get("detail")
    health = payload.get("health")
    with _db_lock, db() as conn:
        if isinstance(health, dict):
            h = {k: health.get(k) for k in ("uptime", "rssi", "reset", "heap", "tx", "ok", "fail", "none", "last_rx", "queue", "fail_streak")}
            conn.execute("INSERT OR REPLACE INTO health (ts, uptime, rssi, reset, heap, nrf, tx, ok, fail, none, last_rx, queue, fail_streak) "
                         "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?)",
                         (int(now), h["uptime"], h["rssi"], str(h["reset"] or "")[:20], h["heap"], 1 if health.get("nrf") else 0,
                          h["tx"], h["ok"], h["fail"], h["none"], h["last_rx"], h["queue"], h["fail_streak"]))
            conn.execute("DELETE FROM health WHERE ts < ?", (int(now) - 30 * 86400,))
        if clean:
            marks = ",".join("?" * (len(FIELDS) + 1))
            conn.executemany(f"INSERT OR REPLACE INTO readings (ts,{','.join(FIELDS)}) VALUES ({marks})", clean)
        if isinstance(detail, dict) and 1_600_000_000 < int(detail.get("ts", 0)) <= now + 300:
            conn.execute("INSERT INTO detail (id, ts, json) VALUES (1, ?, ?) "
                         "ON CONFLICT(id) DO UPDATE SET ts = excluded.ts, json = excluded.json "
                         "WHERE excluded.ts >= detail.ts", (int(detail["ts"]), json.dumps(detail)[:20000]))
    return len(clean)


def latest():
    with db() as conn:
        row = conn.execute("SELECT * FROM readings ORDER BY ts DESC LIMIT 1").fetchone()
    if not row:
        return {"reading": None}
    r = dict(row)
    return {"reading": r, "age_s": int(time.time() - r["ts"])}


def sun_elevation(ts):
    """solar elevation in degrees at the configured location (NOAA approximation)"""
    import math
    d = datetime.fromtimestamp(ts, ZoneInfo("UTC"))
    n = d.timetuple().tm_yday
    g = 2 * math.pi / 365 * (n - 1 + (d.hour - 12) / 24)
    eqt = 229.18 * (0.000075 + 0.001868 * math.cos(g) - 0.032077 * math.sin(g) - 0.014615 * math.cos(2 * g) - 0.040849 * math.sin(2 * g))
    decl = (0.006918 - 0.399912 * math.cos(g) + 0.070257 * math.sin(g) - 0.006758 * math.cos(2 * g) + 0.000907 * math.sin(2 * g)
            - 0.002697 * math.cos(3 * g) + 0.00148 * math.sin(3 * g))
    tst = d.hour * 60 + d.minute + eqt + 4 * LON
    ha, lat = math.radians(tst / 4 - 180), math.radians(LAT)
    cosz = math.sin(lat) * math.sin(decl) + math.cos(lat) * math.cos(decl) * math.cos(ha)
    return 90 - math.degrees(math.acos(max(-1, min(1, cosz))))


def low_light(now):
    """the forecast expects so little output this hour that the inverter may be off (storm, heavy cloud, dusk)"""
    try:
        fc = forecast()
    except OSError:
        return False
    hour = [h for h in fc["hourly"] if h["ts"] - 3600 <= now < h["ts"]]
    return bool(hour) and (hour[0]["est_w"] < 120 or hour[0]["code"] >= 95)


def wifi_label(rssi):
    if rssi is None:
        return "unknown"
    return "strong" if rssi >= -60 else "good" if rssi >= -70 else "weak" if rssi >= -78 else "very weak"


def health():
    """Where is the chain broken? Wi-Fi (roof device -> internet) or radio (inverter -> roof device)."""
    now = time.time()
    with db() as conn:
        rows = [dict(r) for r in conn.execute("SELECT * FROM health WHERE ts >= ? ORDER BY ts", (int(now) - 3600,))]
        last_h = conn.execute("SELECT * FROM health ORDER BY ts DESC LIMIT 1").fetchone()
        last_r = conn.execute("SELECT ts FROM readings ORDER BY ts DESC LIMIT 1").fetchone()
    last_h = dict(last_h) if last_h else None
    contact = max(last_h["ts"] if last_h else 0, last_r["ts"] if last_r else 0)
    contact_age = int(now - contact) if contact else None
    daylight = sun_elevation(now) > 3

    # restarts in the last hour: uptime going backwards between reports
    restarts = sum(1 for a, b in zip(rows, rows[1:]) if b["uptime"] is not None and a["uptime"] is not None and b["uptime"] < a["uptime"])
    # radio success over the last 15 minutes, within the current boot
    win = [r for r in rows if r["ts"] >= now - 900]
    for i in range(len(win) - 1, 0, -1):
        if win[i]["uptime"] < win[i - 1]["uptime"]:
            win = win[i:]
            break
    radio = None
    if len(win) >= 2:
        a, b = win[0], win[-1]
        tx = b["tx"] - a["tx"]
        radio = {"tx": tx, "ok": b["ok"] - a["ok"], "fail": b["fail"] - a["fail"], "none": b["none"] - a["none"],
                 "minutes": round((b["ts"] - a["ts"]) / 60)}
    last_reply_age = int(now - last_h["last_rx"]) if last_h and last_h["last_rx"] else None
    rssi = last_h["rssi"] if last_h else None

    # diagnosis, most important problem first
    def mins(sec):
        return f"{sec // 60} min" if sec < 7200 else f"{sec // 3600} h"
    if contact_age is None:
        state, msg = "unknown", "No data from the roof device yet."
    elif contact_age > 180:
        if rssi is not None and rssi < -75:
            state, msg = "wifi", (f"No contact from the roof device for {mins(contact_age)}. Its Wi-Fi was {wifi_label(rssi)} "
                                  f"({rssi} dBm) when it last reported, so it has most likely lost Wi-Fi.")
        else:
            state, msg = "offline", f"No contact from the roof device for {mins(contact_age)}. It has lost power or Wi-Fi."
    elif restarts >= 2:
        state, msg = "power", (f"The roof device restarted {restarts} times in the last hour (last reason: {last_h['reset']}). "
                               "Check its power supply.")
    elif not daylight:
        state, msg = "night", "The roof device is online. The inverter is asleep until the sun is up."
    elif radio and radio["tx"] >= 4 and radio["ok"] < 0.3 * radio["tx"] and low_light(now):
        state, msg = "dark", (f"The inverter isn't answering ({radio['ok']} of {radio['tx']} requests), but there is very little "
                              "sun right now, so it has probably switched itself off. It wakes up again when the light returns.")
    elif radio and radio["tx"] >= 4 and radio["ok"] < 0.3 * radio["tx"]:
        state, msg = "radio", (f"Wi-Fi is fine, but the inverter answered only {radio['ok']} of {radio['tx']} requests in the last "
                               f"{radio['minutes']} minutes. Move the roof device closer to the inverter.")
    elif last_h and last_reply_age is not None and last_reply_age > 600:
        state, msg = "radio", f"Wi-Fi is fine, but there has been no reply from the inverter for {mins(last_reply_age)}."
    elif rssi is not None and rssi < -75:
        state, msg = "wifi_weak", f"Working, but the Wi-Fi signal is {wifi_label(rssi)} ({rssi} dBm). Uploads may drop out."
    else:
        state, msg = "ok", "Everything is working."
    return {"state": state, "message": msg, "daylight": daylight, "contact_age": contact_age,
            "wifi": {"rssi": rssi, "label": wifi_label(rssi)}, "radio": radio, "last_reply_age": last_reply_age,
            "restarts_last_hour": restarts, "device": {k: last_h[k] for k in ("uptime", "reset", "heap", "queue", "fail_streak", "nrf")} if last_h else None}


def summary():
    """everything a small display needs in one request (the Cardputer display client)"""
    lv, hl = latest(), health()
    today = datetime.now(TZ).date()
    with db() as conn:
        d = daily_energy(conn, today)
    try:
        fc = forecast()
        expected = fc["daily"][0]["est_kwh"] if fc["daily"] else None
        weather = {"temp": fc["current"]["temp"], "code": fc["current"]["code"]}
    except OSError:
        expected, weather = None, None
    r = lv.get("reading") or {}
    return {"pac": r.get("pac"), "age_s": lv.get("age_s"), "panels": [r.get(f"p{i}") for i in range(1, 5)][:CONFIG["inverter"]["panels"]],
            "today_kwh": d["kwh"] if d else 0, "peak_w": d["peak_w"] if d else 0, "expected_kwh": expected, "lifetime_kwh": r.get("yt"),
            "weather": weather, "panel_max_w": CONFIG["inverter"]["panel_max_w"],
            "health": {k: hl[k] for k in ("state", "message", "wifi", "radio", "contact_age", "last_reply_age", "restarts_last_hour")}}


def latest_detail():
    with db() as conn:
        row = conn.execute("SELECT ts, json FROM detail WHERE id = 1").fetchone()
    if not row:
        return {"detail": None}
    return {"detail": json.loads(row["json"]), "age_s": int(time.time() - row["ts"])}


def day_series(day):
    start, end = local_day_bounds(day)
    with db() as conn:
        rows = conn.execute(
            "SELECT (ts/60)*60 AS m, AVG(pac) AS pac, AVG(p1) AS p1, AVG(p2) AS p2, AVG(p3) AS p3, AVG(p4) AS p4 "
            "FROM readings WHERE ts >= ? AND ts < ? GROUP BY m ORDER BY m", (start, end)).fetchall()
    return {"date": day.isoformat(), "points": [dict(r) for r in rows]}


def daily_energy(conn, day):
    """kWh produced on a local day.

    Primary: growth of the inverter's lifetime counter (yt) since the last reading before the day, which
    counts everything even if the DTU started late. The inverter's daily counter (yd) is only used as a
    fallback: it still shows yesterday's total until the inverter wakes up in the morning, so only the
    last yd of the day is trusted, never the maximum."""
    start, end = local_day_bounds(day)
    r = conn.execute(
        "SELECT MAX(yt) AS ytmax, MIN(yt) AS ytmin, MAX(pac) AS peak, COUNT(*) AS n "
        "FROM readings WHERE ts >= ? AND ts < ? AND pac IS NOT NULL", (start, end)).fetchone()
    if not r or not r["n"]:
        return None
    before = conn.execute("SELECT yt FROM readings WHERE ts < ? AND yt > 0 ORDER BY ts DESC LIMIT 1", (start,)).fetchone()
    last_yd = conn.execute("SELECT yd FROM readings WHERE ts >= ? AND ts < ? AND yd IS NOT NULL ORDER BY ts DESC LIMIT 1",
                           (start, end)).fetchone()
    kwh = None
    if r["ytmax"]:
        base = before["yt"] if before and before["yt"] <= r["ytmax"] else r["ytmin"]
        kwh = max(0.0, r["ytmax"] - base)
    if not before and last_yd and last_yd["yd"] is not None:
        # first day of recording: the counter delta misses the hours before the DTU started
        kwh = max(kwh or 0.0, last_yd["yd"] / 1000.0)
    return {"date": day.isoformat(), "kwh": round(kwh or 0, 3), "peak_w": round(r["peak"] or 0, 1), "samples": r["n"]}


def daily(days, end_day=None):
    end_day = end_day or datetime.now(TZ).date()
    out = []
    with db() as conn:
        for i in range(days - 1, -1, -1):
            d = daily_energy(conn, end_day - timedelta(days=i))
            if d:
                out.append(d)
    return {"days": out}


def series(start, end, bucket):
    """average AC power per time bucket (seconds) between two unix timestamps"""
    bucket = max(60, min(bucket, 86400))
    with db() as conn:
        rows = conn.execute(
            "SELECT (ts/?)*? AS m, AVG(pac) AS pac, MAX(pac) AS peak FROM readings "
            "WHERE ts >= ? AND ts < ? GROUP BY m ORDER BY m", (bucket, bucket, start, end)).fetchall()
    return {"from": start, "to": end, "bucket": bucket, "points": [dict(r) for r in rows]}


def monthly():
    with db() as conn:
        first = conn.execute("SELECT MIN(ts) FROM readings").fetchone()[0]
        if not first:
            return {"months": []}
        day = datetime.fromtimestamp(first, TZ).date()
        today = datetime.now(TZ).date()
        months = {}
        while day <= today:
            d = daily_energy(conn, day)
            if d:
                m = months.setdefault(day.strftime("%Y-%m"), {"month": day.strftime("%Y-%m"), "kwh": 0.0, "days": 0})
                m["kwh"] = round(m["kwh"] + d["kwh"], 3)
                m["days"] += 1
            day += timedelta(days=1)
    return {"months": list(months.values())}


def forecast():
    with _forecast_lock:
        if _forecast["data"] and time.time() - _forecast["ts"] < 900:
            return _forecast["data"]
        q = urlencode({
            "latitude": LAT, "longitude": LON, "timezone": CONFIG["timezone"], "forecast_days": 7,
            "tilt": PANEL_TILT, "azimuth": PANEL_AZIMUTH,
            "current": "temperature_2m,weather_code,cloud_cover,is_day",
            "hourly": "temperature_2m,weather_code,cloud_cover,global_tilted_irradiance,is_day",
            "daily": "weather_code,temperature_2m_max,temperature_2m_min,sunrise,sunset",
        })
        raw = json.load(urlopen("https://api.open-meteo.com/v1/forecast?" + q, timeout=15))
        h = raw["hourly"]
        hourly, per_day = [], {}
        for i, t in enumerate(h["time"]):
            ts = int(datetime.strptime(t, "%Y-%m-%dT%H:%M").replace(tzinfo=TZ).timestamp())
            gti = h["global_tilted_irradiance"][i] or 0
            # radiation values are the mean of the preceding hour
            est = min(gti / 1000 * PEAK_W * PERF_RATIO, INVERTER_MAX_W)
            per_day[t[:10]] = per_day.get(t[:10], 0) + est / 1000
            hourly.append({"ts": ts, "temp": h["temperature_2m"][i], "code": h["weather_code"][i],
                           "cloud": h["cloud_cover"][i], "is_day": h["is_day"][i], "est_w": round(est)})
        d = raw["daily"]
        daily = [{"date": day, "code": d["weather_code"][i], "tmax": d["temperature_2m_max"][i],
                  "tmin": d["temperature_2m_min"][i], "sunrise": d["sunrise"][i][11:], "sunset": d["sunset"][i][11:],
                  "est_kwh": round(per_day.get(day, 0), 2)} for i, day in enumerate(d["time"])]
        c = raw["current"]
        data = {"current": {"temp": c["temperature_2m"], "code": c["weather_code"], "cloud": c["cloud_cover"], "is_day": c["is_day"]},
                "hourly": hourly, "daily": daily,
                "assumptions": {"tilt": PANEL_TILT, "azimuth": PANEL_AZIMUTH, "peak_w": PEAK_W, "performance_ratio": PERF_RATIO}}
        _forecast.update(ts=time.time(), data=data)
        return data


def export_csv(kind, start_day, end_day):
    """CSV download: every reading, daily totals or monthly totals for [start_day, end_day]."""
    if end_day < start_day:
        start_day, end_day = end_day, start_day
    out = io.StringIO()
    w = csv.writer(out)
    if kind == "readings":
        start, _ = local_day_bounds(start_day)
        _, end = local_day_bounds(end_day)
        w.writerow(["time_local", "time_utc", "ac_power_w", "yield_today_wh", "yield_total_kwh", "inverter_temp_c",
                    "ac_voltage_v", "frequency_hz"] + [f"panel{i}_{k}" for i in range(1, 5) for k in ("w", "v", "a")])
        with db() as conn:
            for r in conn.execute("SELECT * FROM readings WHERE ts >= ? AND ts < ? ORDER BY ts", (start, end)):
                t = datetime.fromtimestamp(r["ts"], TZ)
                w.writerow([t.strftime("%Y-%m-%d %H:%M:%S"), datetime.fromtimestamp(r["ts"], ZoneInfo("UTC")).strftime("%Y-%m-%dT%H:%M:%SZ"),
                            r["pac"], r["yd"], r["yt"], r["temp"], r["uac"], r["freq"]] +
                           [r[f"{k}{i}"] for i in range(1, 5) for k in ("p", "u", "i")])
    elif kind == "daily":
        w.writerow(["date", "energy_kwh", "peak_w", "readings"])
        with db() as conn:
            day = start_day
            while day <= end_day:
                d = daily_energy(conn, day)
                if d:
                    w.writerow([d["date"], d["kwh"], d["peak_w"], d["samples"]])
                day += timedelta(days=1)
    elif kind == "monthly":
        w.writerow(["month", "energy_kwh", "days_with_data"])
        for m in monthly()["months"]:
            if start_day.strftime("%Y-%m") <= m["month"] <= end_day.strftime("%Y-%m"):
                w.writerow([m["month"], m["kwh"], m["days"]])
    else:
        raise ValueError("kind must be readings, daily or monthly")
    return out.getvalue()


STATIC = {".css": "text/css; charset=utf-8", ".js": "text/javascript; charset=utf-8", ".svg": "image/svg+xml",
          ".jpg": "image/jpeg", ".webp": "image/webp", ".png": "image/png"}


class Handler(BaseHTTPRequestHandler):
    server_version = "solar-monitor"

    def log_message(self, fmt, *args):
        pass

    def send_json(self, obj, code=200):
        body = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        if urlparse(self.path).path != "/api/ingest":
            return self.send_json({"error": "not found"}, 404)
        if not TOKEN or self.headers.get("Authorization", "") != f"Bearer {TOKEN}":
            return self.send_json({"error": "unauthorized"}, 401)
        try:
            length = min(int(self.headers.get("Content-Length", 0)), 256 * 1024)
            n = ingest(json.loads(self.rfile.read(length)))
        except (ValueError, json.JSONDecodeError) as e:
            return self.send_json({"error": str(e)}, 400)
        self.send_json({"stored": n})

    def do_GET(self):
        url = urlparse(self.path)
        q = parse_qs(url.query)
        try:
            if url.path == "/api/live":
                return self.send_json(latest())
            if url.path == "/api/summary":
                return self.send_json(summary())
            if url.path == "/api/health":
                return self.send_json(health())
            if url.path == "/api/config":
                return self.send_json(public_config())
            if url.path == "/api/forecast":
                try:
                    return self.send_json(forecast())
                except OSError as e:
                    return self.send_json({"error": f"forecast unavailable: {e}"}, 502)
            if url.path == "/api/detail":
                return self.send_json(latest_detail())
            if url.path == "/api/day":
                d = q.get("date", [None])[0]
                day = datetime.strptime(d, "%Y-%m-%d").date() if d else datetime.now(TZ).date()
                return self.send_json(day_series(day))
            if url.path == "/api/daily":
                end = datetime.strptime(q["to"][0], "%Y-%m-%d").date() if "to" in q else None
                return self.send_json(daily(min(int(q.get("days", ["60"])[0]), 730), end))
            if url.path == "/api/series":
                start, end = int(q["from"][0]), int(q["to"][0])
                if end - start > 40 * 86400:
                    raise ValueError("range too long")
                return self.send_json(series(start, end, int(q.get("bucket", ["900"])[0])))
            if url.path == "/api/monthly":
                return self.send_json(monthly())
            if url.path == "/api/export.csv":
                kind = q.get("kind", ["daily"])[0]
                today = datetime.now(TZ).date()
                start = datetime.strptime(q["from"][0], "%Y-%m-%d").date() if "from" in q else today - timedelta(days=30)
                end = datetime.strptime(q["to"][0], "%Y-%m-%d").date() if "to" in q else today
                if (end - start).days > 3700:
                    raise ValueError("range too long")
                body = export_csv(kind, start, end).encode()
                self.send_response(200)
                self.send_header("Content-Type", "text/csv; charset=utf-8")
                self.send_header("Content-Disposition", f'attachment; filename="solar-{kind}-{start}-to-{end}.csv"')
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                return self.wfile.write(body)
        except (ValueError, KeyError) as e:
            return self.send_json({"error": str(e)}, 400)
        pages = {"/": "index.html", "/index.html": "index.html", "/live": "live.html"}
        if url.path in pages:
            body = (WEB_DIR / pages[url.path]).read_bytes()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            return self.wfile.write(body)
        ext = os.path.splitext(url.path)[1]
        target = (WEB_DIR / url.path.lstrip("/")).resolve()
        if ext in STATIC and target.is_file() and WEB_DIR.resolve() in target.parents:
            body = target.read_bytes()
            self.send_response(200)
            self.send_header("Content-Type", STATIC[ext])
            self.send_header("Cache-Control", "public, max-age=86400" if ext in (".jpg", ".webp", ".png") else "public, max-age=300")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            return self.wfile.write(body)
        self.send_json({"error": "not found"}, 404)


if __name__ == "__main__":
    init_db()
    print(f"solar-monitor on 127.0.0.1:{PORT}, db {DB_PATH}", flush=True)
    ThreadingHTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
