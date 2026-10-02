// Cardputer desk display for the solar monitor: shows /api/summary from the server.
// G0 (side button): short press = next page, hold = brightness.
#define LGFX_USE_V1
#include <Arduino.h>
#include <LovyanGFX.hpp>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// Let's Encrypt roots (ISRG Root X1 + X2), for https servers
static const char ROOT_CA[] =
    "-----BEGIN CERTIFICATE-----\n"
    "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\n"
    "-----END CERTIFICATE-----\n"
    "-----BEGIN CERTIFICATE-----\n"
    "MIICGzCCAaGgAwIBAgIQQdKd0XLq7qeAwSxs6S+HUjAKBggqhkjOPQQDAzBPMQsw\n"
    "CQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJuZXQgU2VjdXJpdHkgUmVzZWFyY2gg\n"
    "R3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBYMjAeFw0yMDA5MDQwMDAwMDBaFw00\n"
    "MDA5MTcxNjAwMDBaME8xCzAJBgNVBAYTAlVTMSkwJwYDVQQKEyBJbnRlcm5ldCBT\n"
    "ZWN1cml0eSBSZXNlYXJjaCBHcm91cDEVMBMGA1UEAxMMSVNSRyBSb290IFgyMHYw\n"
    "EAYHKoZIzj0CAQYFK4EEACIDYgAEzZvVn4CDCuwJSvMWSj5cz3es3mcFDR0HttwW\n"
    "+1qLFNvicWDEukWVEYmO6gbf9yoWHKS5xcUy4APgHoIYOIvXRdgKam7mAHf7AlF9\n"
    "ItgKbppbd9/w+kHsOdx1ymgHDB/qo0IwQDAOBgNVHQ8BAf8EBAMCAQYwDwYDVR0T\n"
    "AQH/BAUwAwEB/zAdBgNVHQ4EFgQUfEKWrt5LSDv6kviejM9ti6lyN5UwCgYIKoZI\n"
    "zj0EAwMDaAAwZQIwe3lORlCEwkSHRhtFcP9Ymd70/aTSVaYgLXTWNLxBo1BfASdW\n"
    "tL4ndQavEi51mI38AjEAi/V3bNTIZargCyzuFJ0nN6T5U6VR5CmD1/iQMVtCnwr1\n"
    "/q4AaOeMSQ+2b1tbFfLn\n"
    "-----END CERTIFICATE-----\n";

class CardputerLgfx : public lgfx::LGFX_Device {
    lgfx::Panel_ST7789 panel; lgfx::Bus_SPI bus; lgfx::Light_PWM light;
  public:
    CardputerLgfx() {
        auto b = bus.config();
        b.spi_host = SPI3_HOST; b.spi_mode = 0; b.freq_write = 40000000; b.spi_3wire = true; b.use_lock = true;
        b.dma_channel = SPI_DMA_CH_AUTO; b.pin_sclk = 36; b.pin_mosi = 35; b.pin_miso = -1; b.pin_dc = 34;
        bus.config(b); panel.setBus(&bus);
        auto p = panel.config();
        p.pin_cs = 37; p.pin_rst = 33; p.pin_busy = -1; p.memory_width = 240; p.memory_height = 320;
        p.panel_width = 135; p.panel_height = 240; p.offset_x = 52; p.offset_y = 40; p.readable = false; p.invert = true;
        panel.config(p);
        auto l = light.config(); l.pin_bl = 38; l.freq = 256; l.pwm_channel = 7; light.config(l); panel.setLight(&light);
        setPanel(&panel);
    }
};

static CardputerLgfx lcd;
static LGFX_Sprite spr(&lcd);
static const uint16_t SUN = 0xFE8D, INK2 = 0xBDF7, MUTED = 0x7BEF, BG = 0x0861, GOOD = 0x7F95, WARN = 0xFE8D, BAD = 0xFBCE;
static const uint8_t BRIGHT[4] = {140, 255, 50, 10};

static StaticJsonDocument<3072> doc;
static bool haveData = false;
static String lastError = "Connecting to Wi-Fi";
static uint32_t lastFetch = 0, fetchedAtMs = 0;
static uint8_t page = 0, bright = 0;

static const char *weather(int c) {
    if (c == 0) return "clear"; if (c <= 2) return "partly cloudy"; if (c == 3) return "overcast"; if (c <= 48) return "fog";
    if (c <= 57) return "drizzle"; if (c <= 67) return "rain"; if (c <= 77) return "snow"; if (c <= 82) return "showers"; return "storms";
}
static uint16_t stateColor(const char *st) {
    if (!strcmp(st, "ok") || !strcmp(st, "night")) return GOOD;
    if (!strcmp(st, "wifi_weak")) return WARN;
    return BAD;
}
static String ago(long s) {
    if (s < 0) return "-";
    if (s < 120) return String(s) + " s ago";
    if (s < 7200) return String(s / 60) + " min ago";
    return String(s / 3600) + " h ago";
}

static bool fetch() {
    if (WiFi.status() != WL_CONNECTED) { lastError = "No Wi-Fi"; return false; }
    String url = String(SERVER_URL) + "/api/summary";
    HTTPClient http;
    WiFiClientSecure secure; WiFiClient plain;
    bool tls = url.startsWith("https://");
    if (tls) secure.setCACert(ROOT_CA);
    http.setTimeout(8000);
    if (!(tls ? http.begin(secure, url) : http.begin(plain, url))) { lastError = "Bad server URL"; return false; }
    int code = http.GET();
    if (code != 200) { lastError = "Server error " + String(code); Serial.println(lastError); http.end(); return false; }
    DeserializationError e = deserializeJson(doc, http.getStream());
    http.end();
    if (e) { lastError = String("Bad data: ") + e.c_str(); return false; }
    haveData = true; fetchedAtMs = millis(); lastError = "";
    Serial.printf("ok: %.0f W, today %.2f kWh, state %s, wifi %d dBm\n", (float)(doc["pac"] | 0.0f), (float)(doc["today_kwh"] | 0.0f),
                  (const char *)(doc["health"]["state"] | "?"), WiFi.RSSI());
    return true;
}

static void header(const char *title) {
    spr.fillRect(0, 0, 240, 18, 0x10A2);
    spr.setFont(&fonts::Font2); spr.setTextDatum(textdatum_t::middle_left); spr.setTextColor(SUN);
    spr.drawString(title, 6, 9);
    spr.setTextDatum(textdatum_t::middle_right); spr.setTextColor(MUTED);
    spr.drawString(String(page + 1) + "/2", 234, 9);
    if (haveData) {   // overall status dot
        spr.fillCircle(206, 9, 4, stateColor(doc["health"]["state"] | "unknown"));
    }
}

static void pageNow() {
    header("Solar");
    if (!haveData) {
        spr.setFont(&fonts::Font4); spr.setTextDatum(textdatum_t::middle_center); spr.setTextColor(INK2);
        spr.drawString(lastError, 120, 70);
        return;
    }
    long age = (doc["age_s"] | -1L) + (millis() - fetchedAtMs) / 1000;
    bool fresh = age >= 0 && age < 180;
    float pac = doc["pac"] | 0.0f, today = doc["today_kwh"] | 0.0f;
    // big number: live watts, or today's energy when the roof is quiet
    spr.setTextDatum(textdatum_t::baseline_left); spr.setTextColor(TFT_WHITE);
    spr.setFont(&fonts::FreeSansBold24pt7b);
    String big = fresh ? String((int)(pac + 0.5f)) : String(today, 2);
    spr.drawString(big, 6, 66);
    int w = spr.textWidth(big);
    spr.setFont(&fonts::FreeSans9pt7b); spr.setTextColor(INK2);
    spr.drawString(fresh ? "W" : "kWh today", 12 + w, 66);
    // today vs expected, weather
    spr.setFont(&fonts::Font2); spr.setTextDatum(textdatum_t::top_left); spr.setTextColor(INK2);
    String line = String("Today ") + String(today, 2) + " kWh";
    if (!doc["expected_kwh"].isNull()) line += String(" of ~") + String((float)doc["expected_kwh"], 1);
    spr.drawString(line, 6, 72);
    if (!doc["weather"].isNull()) {
        spr.setTextDatum(textdatum_t::top_right);
        spr.drawString(String((int)doc["weather"]["temp"]) + "C " + weather(doc["weather"]["code"]), 234, 72);
    }
    // four panels
    JsonArray ps = doc["panels"].as<JsonArray>();
    int n = ps.size(); float mx = doc["panel_max_w"] | 380.0f;
    if (n > 0) {
        int gap = 6, bw = (228 - gap * (n - 1)) / n;
        for (int i = 0; i < n; i++) {
            float p = fresh ? (ps[i] | 0.0f) : 0; int x = 6 + i * (bw + gap);
            spr.fillRoundRect(x, 92, bw, 14, 4, 0x2124);
            int fw = constrain((int)(p / mx * bw), 0, bw);
            if (fw > 0) spr.fillRoundRect(x, 92, fw, 14, 4, SUN);
            spr.setTextDatum(textdatum_t::middle_center); spr.setTextColor(fw > bw / 2 ? 0x18E3 : TFT_WHITE);
            spr.drawString(String((int)(p + 0.5f)) + "W", x + bw / 2, 99);
        }
    }
    // status
    const char *st = doc["health"]["state"] | "unknown";
    spr.setFont(&fonts::Font0); spr.setTextDatum(textdatum_t::bottom_left); spr.setTextColor(stateColor(st));
    String msg = doc["health"]["message"] | "";
    if (!strcmp(st, "ok")) msg = fresh ? "Live from the roof" : "Roof online";
    if (msg.length() > 38) msg = msg.substring(0, 37) + ".";
    spr.drawString(msg, 6, 131);
    spr.setTextDatum(textdatum_t::bottom_right); spr.setTextColor(MUTED);
    spr.drawString(ago(age), 234, 131);
}

static void wrapped(const String &s, int x, int y, int maxChars, int lh) {
    int start = 0;
    while (start < (int)s.length()) {
        int end = min((int)s.length(), start + maxChars);
        if (end < (int)s.length()) { int sp = s.lastIndexOf(' ', end); if (sp > start) end = sp; }
        spr.drawString(s.substring(start, end), x, y); y += lh; start = end + 1;
    }
}

static void pageLink() {
    header("Connection");
    spr.setFont(&fonts::Font2); spr.setTextDatum(textdatum_t::top_left);
    if (!haveData) { spr.setTextColor(INK2); spr.drawString(lastError, 6, 26); return; }
    JsonObject h = doc["health"];
    int y = 24;
    spr.setTextColor(INK2); spr.drawString("Roof Wi-Fi", 6, y); spr.setTextColor(TFT_WHITE);
    spr.drawString(h["wifi"]["rssi"].isNull() ? String("-") : String((int)h["wifi"]["rssi"]) + " dBm, " + String((const char *)(h["wifi"]["label"] | "")), 96, y); y += 17;
    spr.setTextColor(INK2); spr.drawString("Inverter", 6, y); spr.setTextColor(TFT_WHITE);
    if (!h["radio"].isNull() && (int)h["radio"]["tx"] > 0)
        spr.drawString(String((int)h["radio"]["ok"]) + " of " + String((int)h["radio"]["tx"]) + " answered", 96, y);
    else spr.drawString("-", 96, y);
    y += 17;
    spr.setTextColor(INK2); spr.drawString("Last contact", 6, y); spr.setTextColor(TFT_WHITE);
    spr.drawString(h["contact_age"].isNull() ? String("-") : ago((long)h["contact_age"] + (millis() - fetchedAtMs) / 1000), 96, y); y += 17;
    spr.setTextColor(INK2); spr.drawString("This display", 6, y); spr.setTextColor(TFT_WHITE);
    spr.drawString(String(WiFi.RSSI()) + " dBm, " + WiFi.localIP().toString(), 96, y); y += 19;
    spr.setTextColor(stateColor(h["state"] | "unknown")); spr.setFont(&fonts::Font0);
    wrapped(String((const char *)(h["message"] | "")), 6, y, 39, 10);
}

static void draw() {
    spr.fillScreen(BG);
    if (page == 0) pageNow(); else pageLink();
    spr.pushSprite(0, 0);
}

void setup() {
    Serial.begin(115200);
    pinMode(0, INPUT_PULLUP);
    lcd.init(); lcd.setRotation(1); lcd.setBrightness(BRIGHT[bright]);
    spr.setColorDepth(16); spr.createSprite(240, 135);
    draw();
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
}

void loop() {
    static bool down = false, longDone = false; static uint32_t t0 = 0, lastDraw = 0;
    bool pressed = digitalRead(0) == LOW; uint32_t now = millis();
    if (pressed && !down) { down = true; t0 = now; longDone = false; }
    else if (pressed && down && !longDone && now - t0 > 900) { longDone = true; bright = (bright + 1) % 4; lcd.setBrightness(BRIGHT[bright]); }
    else if (!pressed && down) { down = false; if (!longDone && now - t0 > 30) { page = (page + 1) % 2; draw(); } }

    if (WiFi.status() == WL_CONNECTED && (lastFetch == 0 || now - lastFetch > 15000)) {
        lastFetch = now; fetch(); draw();
    }
    if (now - lastDraw > 1000) { lastDraw = now; draw(); }
    delay(10);
}
