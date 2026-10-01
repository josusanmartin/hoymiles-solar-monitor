// Background photo that follows the sun and the weather.
// Photos and their credits come from "photos" in config.json; ?photo=day|dusk|night|overcast|rain|storm forces one.
(function () {
  var LAT = 0, LON = 0, PHOTOS = {}, code = null;
  function elevation(d) {
    d = d || new Date();
    var n = (d - Date.UTC(d.getUTCFullYear(), 0, 0)) / 86400e3, g = 2 * Math.PI / 365 * (n - 1);
    var eqt = 229.18 * (0.000075 + 0.001868 * Math.cos(g) - 0.032077 * Math.sin(g) - 0.014615 * Math.cos(2 * g) - 0.040849 * Math.sin(2 * g));
    var decl = 0.006918 - 0.399912 * Math.cos(g) + 0.070257 * Math.sin(g) - 0.006758 * Math.cos(2 * g) + 0.000907 * Math.sin(2 * g) - 0.002697 * Math.cos(3 * g) + 0.00148 * Math.sin(3 * g);
    var tst = d.getUTCHours() * 60 + d.getUTCMinutes() + eqt + 4 * LON, ha = (tst / 4 - 180) * Math.PI / 180, lat = LAT * Math.PI / 180;
    return 90 - Math.acos(Math.sin(lat) * Math.sin(decl) + Math.cos(lat) * Math.cos(decl) * Math.cos(ha)) * 180 / Math.PI;
  }
  // WMO weather code + sun elevation -> which photo, falling back to "day" when one is missing
  function pick() {
    var forced = new URLSearchParams(location.search).get("photo");
    var el = elevation(), dark = el < -6, low = el < 12, c = code;
    var storm = c != null && c >= 95, rain = c != null && ((c >= 51 && c <= 67) || (c >= 80 && c <= 82)), grey = c === 3 || (c >= 45 && c <= 48);
    var p = forced || (storm ? "storm" : rain ? "rain" : dark ? "night" : low ? "dusk" : grey ? "overcast" : "day");
    if (!PHOTOS[p]) p = PHOTOS.day ? "day" : Object.keys(PHOTOS)[0];
    var f = "none";
    if (!forced && dark && p === "rain") f = "brightness(.45) saturate(.7)";
    if (!forced && !dark && p === "storm") f = "brightness(1.15)";
    if (!forced && low && !dark && grey) f = "saturate(.6) brightness(.85)";
    return { p: p, f: f };
  }
  function paint() {
    var bg = document.getElementById("bg"); if (!bg) return;
    var r = pick(), ph = PHOTOS[r.p], cr = document.getElementById("credit");
    if (!ph) { if (cr) cr.innerHTML = 'Weather from <a href="https://open-meteo.com/">Open-Meteo</a>.'; return; }
    bg.style.backgroundImage = "url(/img/" + ph.file + ")";
    bg.style.backgroundPosition = ph.position || "center";
    bg.style.filter = r.f;
    if (cr) cr.innerHTML = "Photo: " + (ph.source ? '<a href="' + ph.source + '">' + ph.title + "</a>" : ph.title) +
      (ph.author ? " by " + ph.author : "") + (ph.license ? ", " + ph.license : "") + '. Weather from <a href="https://open-meteo.com/">Open-Meteo</a>.';
  }
  window.solarBg = { setWeather: function (c) { code = c; paint(); }, elevation: elevation };
  Promise.all([window.siteConfig, new Promise(function (ok) { document.addEventListener("DOMContentLoaded", ok); })]).then(function (res) {
    var cfg = res[0]; LAT = cfg.latitude; LON = cfg.longitude; PHOTOS = cfg.photos || {};
    paint(); setInterval(paint, 60000);
    fetch("/api/forecast").then(function (r) { return r.json(); }).then(function (f) { if (f.current) { code = f.current.code; paint(); } }).catch(function () {});
  });
})();
