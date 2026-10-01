//-----------------------------------------------------------------------------
// SolarPush: uploads live inverter readings to a remote history server over HTTPS
// readings are queued in the main loop and sent from a separate task, so a slow
// TLS handshake never blocks the radio; failed uploads are retried
//-----------------------------------------------------------------------------

#ifndef __SOLAR_PUSH_H__
#define __SOLAR_PUSH_H__

#if defined(PLUGIN_SOLARPUSH)

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <freertos/queue.h>

#include "../../appInterface.h"
#include "../../hm/hmSystem.h"

#define SOLAR_PUSH_QUEUE_LEN    64
#define SOLAR_PUSH_BATCH_MAX    40

// Let's Encrypt roots (ISRG Root X1 + X2)
static const char SOLAR_PUSH_CA[] =
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

typedef struct {
    uint32_t ts;
    float pac, yd, yt, temp, uac, freq;
    float p[4], u[4], i[4];
} solarReading_t;

// latest full snapshot for the live detail page
typedef struct {
    uint32_t ts;
    uint16_t alarms, maxPwr;
    float limitPct;
    float ac[12];       // U_AC I_AC P_AC F_AC PF_AC Temp YieldTotal YieldDay P_DC Efficiency Q_AC MaxPower
    float dc[4][7];     // U_DC I_DC P_DC YieldDay YieldTotal Irradiation MaxPower
} solarDetail_t;

template <class HMSYSTEM>
class SolarPush {
    public:
        void setup(IApp *app, HMSYSTEM *sys) {
            mApp = app;
            mSys = sys;
            if(0 != strncmp(DEF_PUSH_URL, "http", 4)) {
                DPRINTLN(DBG_WARN, F("SolarPush: no server URL configured (SOLAR_PUSH_URL), uploads disabled"));
                return;
            }
            mQueue = xQueueCreate(SOLAR_PUSH_QUEUE_LEN, sizeof(solarReading_t));
            xTaskCreatePinnedToCore(task, "solarPush", 8192, this, 1, nullptr, 0);
        }

        // main loop: queue every new reading of the first inverter
        void tickSecond() {
            if(nullptr == mQueue)
                return;
            Inverter<> *iv = mSys->getInverterByPos(0);
            if(nullptr == iv)
                return;
            record_t<> *rec = iv->getRecordStruct(RealTimeRunData_Debug);
            if((rec->ts == mLastTs) || (rec->ts < 1600000000UL) || !iv->isAvailable())
                return;
            mLastTs = rec->ts;

            solarReading_t r;
            r.ts   = rec->ts;
            r.pac  = iv->getChannelFieldValue(CH0, FLD_PAC, rec);
            r.yd   = iv->getChannelFieldValue(CH0, FLD_YD, rec);
            r.yt   = iv->getChannelFieldValue(CH0, FLD_YT, rec);
            r.temp = iv->getChannelFieldValue(CH0, FLD_T, rec);
            r.uac  = iv->getChannelFieldValue(CH0, FLD_UAC, rec);
            r.freq = iv->getChannelFieldValue(CH0, FLD_F, rec);
            for(uint8_t ch = 0; ch < 4; ch++) {
                bool has = (ch < iv->channels);
                r.p[ch] = has ? iv->getChannelFieldValue(ch + 1, FLD_PDC, rec) : 0;
                r.u[ch] = has ? iv->getChannelFieldValue(ch + 1, FLD_UDC, rec) : 0;
                r.i[ch] = has ? iv->getChannelFieldValue(ch + 1, FLD_IDC, rec) : 0;
            }

            solarDetail_t dt;
            dt.ts = rec->ts;
            dt.alarms = iv->alarmCnt;
            dt.maxPwr = iv->getMaxPower();
            record_t<> *cfgRec = iv->getRecordStruct(SystemConfigPara);
            dt.limitPct = (cfgRec->ts > 0) ? iv->getChannelFieldValue(CH0, FLD_ACT_ACTIVE_PWR_LIMIT, cfgRec) : -1;
            const uint8_t acFld[12] = {FLD_UAC, FLD_IAC, FLD_PAC, FLD_F, FLD_PF, FLD_T, FLD_YT, FLD_YD, FLD_PDC, FLD_EFF, FLD_Q, FLD_MP};
            for(uint8_t k = 0; k < 12; k++)
                dt.ac[k] = iv->getChannelFieldValue(CH0, acFld[k], rec);
            const uint8_t dcFld[7] = {FLD_UDC, FLD_IDC, FLD_PDC, FLD_YD, FLD_YT, FLD_IRR, FLD_MP};
            for(uint8_t ch = 0; ch < 4; ch++)
                for(uint8_t k = 0; k < 7; k++)
                    dt.dc[ch][k] = (ch < iv->channels) ? iv->getChannelFieldValue(ch + 1, dcFld[k], rec) : 0;
            portENTER_CRITICAL(&mMux);
            mDetail = dt;
            portEXIT_CRITICAL(&mMux);

            if(pdTRUE != xQueueSend(mQueue, &r, 0)) {
                solarReading_t drop;
                xQueueReceive(mQueue, &drop, 0);  // full: drop the oldest
                xQueueSend(mQueue, &r, 0);
            }
        }

        uint32_t getSent() const { return mSent; }
        int getLastCode() const { return mLastCode; }

    private:
        static void task(void *arg) {
            static_cast<SolarPush*>(arg)->run();
        }

        void run() {
            static solarReading_t batch[SOLAR_PUSH_BATCH_MAX];
            uint8_t n = 0;
            uint32_t backoff = 5000;
            for(;;) {
                // collect whatever is queued (wait up to 10s for the first one)
                solarReading_t r;
                if((n == 0) && (pdTRUE != xQueueReceive(mQueue, &r, pdMS_TO_TICKS(10000))))
                    continue;
                if(n == 0)
                    batch[n++] = r;
                while((n < SOLAR_PUSH_BATCH_MAX) && (pdTRUE == xQueueReceive(mQueue, &r, 0)))
                    batch[n++] = r;

                if(WiFi.status() != WL_CONNECTED) {
                    vTaskDelay(pdMS_TO_TICKS(5000));
                    continue;
                }
                if(post(batch, n)) {
                    mSent += n;
                    n = 0;
                    backoff = 5000;
                } else {
                    vTaskDelay(pdMS_TO_TICKS(backoff));
                    if(backoff < 60000)
                        backoff *= 2;
                }
            }
        }

        bool post(const solarReading_t *b, uint8_t n) {
            String body;
            body.reserve(1400 + n * 260);
            body = F("{\"device\":\"ahoy\",\"readings\":[");
            char buf[280];
            for(uint8_t k = 0; k < n; k++) {
                const solarReading_t &r = b[k];
                snprintf(buf, sizeof(buf),
                    "%s{\"ts\":%lu,\"pac\":%.1f,\"yd\":%.0f,\"yt\":%.3f,\"temp\":%.1f,\"uac\":%.1f,\"freq\":%.2f,"
                    "\"p1\":%.1f,\"p2\":%.1f,\"p3\":%.1f,\"p4\":%.1f,\"u1\":%.1f,\"u2\":%.1f,\"u3\":%.1f,\"u4\":%.1f,"
                    "\"i1\":%.2f,\"i2\":%.2f,\"i3\":%.2f,\"i4\":%.2f}",
                    k ? "," : "", (unsigned long)r.ts, r.pac, r.yd, r.yt, r.temp, r.uac, r.freq,
                    r.p[0], r.p[1], r.p[2], r.p[3], r.u[0], r.u[1], r.u[2], r.u[3], r.i[0], r.i[1], r.i[2], r.i[3]);
                body += buf;
            }
            body += F("]");

            solarDetail_t dt;
            portENTER_CRITICAL(&mMux);
            dt = mDetail;
            portEXIT_CRITICAL(&mMux);
            if(dt.ts > 0) {
                static const char *acName[12] = {"U_AC", "I_AC", "P_AC", "F_AC", "PF_AC", "Temp", "YieldTotal", "YieldDay", "P_DC", "Efficiency", "Q_AC", "MaxPower"};
                static const char *dcName[7] = {"U_DC", "I_DC", "P_DC", "YieldDay", "YieldTotal", "Irradiation", "MaxPower"};
                Inverter<> *iv = mSys->getInverterByPos(0);
                char ser[13] = "";
                if(nullptr != iv)
                    snprintf(ser, sizeof(ser), "%012llx", (unsigned long long)iv->config->serial.u64);
                snprintf(buf, sizeof(buf), ",\"detail\":{\"ts\":%lu,\"name\":\"%s\",\"serial\":\"%s\",\"alarms\":%u,\"max_pwr\":%u",
                    (unsigned long)dt.ts, (nullptr != iv) ? iv->config->name : "", ser, dt.alarms, dt.maxPwr);
                body += buf;
                if(dt.limitPct >= 0) {
                    snprintf(buf, sizeof(buf), ",\"limit_pct\":%.1f", dt.limitPct);
                    body += buf;
                }
                body += F(",\"ac\":{");
                for(uint8_t k = 0; k < 12; k++) {
                    snprintf(buf, sizeof(buf), "%s\"%s\":%.3f", k ? "," : "", acName[k], dt.ac[k]);
                    body += buf;
                }
                body += F("},\"channels\":[");
                for(uint8_t ch = 0; ch < 4; ch++) {
                    body += ch ? F(",{") : F("{");
                    for(uint8_t k = 0; k < 7; k++) {
                        snprintf(buf, sizeof(buf), "%s\"%s\":%.3f", k ? "," : "", dcName[k], dt.dc[ch][k]);
                        body += buf;
                    }
                    body += F("}");
                }
                body += F("]}");
            }
            body += F("}");

            // https with the Let's Encrypt roots, or plain http for a server on the local network
            bool tls = (0 == strncmp(DEF_PUSH_URL, "https://", 8));
            WiFiClientSecure secure;
            WiFiClient plain;
            secure.setCACert(SOLAR_PUSH_CA);
            secure.setTimeout(10);
            HTTPClient http;
            http.setTimeout(10000);
            if(!(tls ? http.begin(secure, DEF_PUSH_URL) : http.begin(plain, DEF_PUSH_URL))) {
                mLastCode = -100;
                return false;
            }
            http.addHeader(F("Content-Type"), F("application/json"));
            http.addHeader(F("Authorization"), String(F("Bearer ")) + DEF_PUSH_TOKEN);
            mLastCode = http.POST(body);
            http.end();
            return (200 == mLastCode);
        }

        IApp *mApp = nullptr;
        HMSYSTEM *mSys = nullptr;
        QueueHandle_t mQueue = nullptr;
        uint32_t mLastTs = 0;
        solarDetail_t mDetail = {};
        portMUX_TYPE mMux = portMUX_INITIALIZER_UNLOCKED;
        volatile uint32_t mSent = 0;
        volatile int mLastCode = 0;
};

#endif /*PLUGIN_SOLARPUSH*/

#endif /*__SOLAR_PUSH_H__*/
