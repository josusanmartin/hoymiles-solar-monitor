// Loads the site configuration (/api/config) once; pages and bg.js wait on window.siteConfig.
// Also provides timezone helpers that work for any IANA zone, including daylight saving.
window.siteConfig = fetch("/api/config").then(function (r) { return r.json(); });
window.tzOffset = function (tz, ts) {   // seconds the zone is ahead of UTC at unix time ts
  var p = {};
  new Intl.DateTimeFormat("en-US", { timeZone: tz, hourCycle: "h23", year: "numeric", month: "2-digit", day: "2-digit",
    hour: "2-digit", minute: "2-digit", second: "2-digit" }).formatToParts(new Date(ts * 1000)).forEach(function (x) { p[x.type] = x.value; });
  return Date.UTC(+p.year, p.month - 1, +p.day, +p.hour, +p.minute, +p.second) / 1000 - Math.floor(ts);
};
