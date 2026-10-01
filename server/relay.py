#!/usr/bin/env python3
"""Relay: polls an AhoyDTU's local API and forwards new readings to the solar monitor server.
Use it when the DTU runs stock AhoyDTU firmware (without the SolarPush plugin).

    SOLAR_URL=https://solar.example.com SOLAR_TOKEN=... python3 relay.py http://<dtu-ip>
"""
import json, os, sys, time, urllib.request
DTU = sys.argv[1] if len(sys.argv) > 1 else os.environ.get("SOLAR_DTU", "http://192.168.4.1")
URL = os.environ.get("SOLAR_URL", "http://127.0.0.1:8742").rstrip("/") + "/api/ingest"
TOKEN = os.environ.get("SOLAR_TOKEN") or open(os.path.expanduser("~/.config/solar-monitor/token")).read().strip()
AC = ["U_AC", "I_AC", "P_AC", "F_AC", "PF_AC", "Temp", "YieldTotal", "YieldDay", "P_DC", "Efficiency", "Q_AC", "MaxPower", "MaxTemp"]
DC = ["U_DC", "I_DC", "P_DC", "YieldDay", "YieldTotal", "Irradiation", "MaxPower"]
last = 0
while True:
    try:
        d = json.load(urllib.request.urlopen(DTU + "/api/inverter/id/0", timeout=8))
        ts, c = d["ts_last_success"], d["ch"]
        if ts > last and ts > 1_600_000_000:
            r = {"ts": ts, "pac": c[0][2], "freq": c[0][3], "temp": c[0][5], "yt": c[0][6], "yd": c[0][7], "uac": c[0][0]}
            for i in range(4):
                r[f"u{i+1}"], r[f"i{i+1}"], r[f"p{i+1}"] = c[i + 1][0], c[i + 1][1], c[i + 1][2]
            detail = {"ts": ts, "name": d.get("name"), "serial": d.get("serial"), "alarms": d.get("alarm_cnt"),
                      "rssi": d.get("rssi"), "limit_pct": d.get("power_limit_read"), "max_pwr": d.get("max_pwr"),
                      "ac": dict(zip(AC, c[0])),
                      "channels": [dict(zip(DC, ch)) for ch in c[1:5]]}
            req = urllib.request.Request(URL, data=json.dumps({"readings": [r], "detail": detail}).encode(), method="POST",
                headers={"Authorization": "Bearer " + TOKEN, "Content-Type": "application/json"})
            urllib.request.urlopen(req, timeout=15).read()
            last = ts
            print(time.strftime("%H:%M:%S"), "sent", ts, r["pac"], "W", flush=True)
    except Exception as e:
        print(time.strftime("%H:%M:%S"), "skip:", type(e).__name__, flush=True)
    time.sleep(15)
