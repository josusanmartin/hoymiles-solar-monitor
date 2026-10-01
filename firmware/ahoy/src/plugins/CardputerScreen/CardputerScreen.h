//-----------------------------------------------------------------------------
// M5Stack Cardputer ADV built-in display (ST7789V2 135x240)
// shows live inverter data; G0 button (side) switches between pages
//-----------------------------------------------------------------------------

#ifndef __CARDPUTER_SCREEN_H__
#define __CARDPUTER_SCREEN_H__

#if defined(CARDPUTER_ADV)

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <WiFi.h>

#include "../../appInterface.h"
#include "../../config/settings.h"
#include "../../hm/hmSystem.h"
#include "../../hm/Radio.h"

#define CARD_SCR_W      240
#define CARD_SCR_H      135
#define CARD_BTN_PIN    0
#define CARD_NUM_PAGES  2

class CardputerLgfx : public lgfx::LGFX_Device {
    public:
        CardputerLgfx() {
            {
                auto cfg = mBus.config();
                cfg.spi_host    = SPI3_HOST;    // SPI2_HOST is used by the nRF24 (spiPatcher)
                cfg.spi_mode    = 0;
                cfg.freq_write  = 40000000;
                cfg.freq_read   = 16000000;
                cfg.spi_3wire   = true;
                cfg.use_lock    = true;
                cfg.dma_channel = SPI_DMA_CH_AUTO;
                cfg.pin_sclk    = 36;
                cfg.pin_mosi    = 35;
                cfg.pin_miso    = -1;
                cfg.pin_dc      = 34;
                mBus.config(cfg);
                mPanel.setBus(&mBus);
            }
            {
                auto cfg = mPanel.config();
                cfg.pin_cs           = 37;
                cfg.pin_rst          = 33;
                cfg.pin_busy         = -1;
                cfg.memory_width     = 240;
                cfg.memory_height    = 320;
                cfg.panel_width      = 135;
                cfg.panel_height     = 240;
                cfg.offset_x         = 52;
                cfg.offset_y         = 40;
                cfg.offset_rotation  = 0;
                cfg.readable         = false;
                cfg.invert           = true;
                cfg.rgb_order        = false;
                cfg.dlen_16bit       = false;
                cfg.bus_shared       = false;
                mPanel.config(cfg);
            }
            {
                auto cfg = mLight.config();
                cfg.pin_bl      = 38;
                cfg.invert      = false;
                cfg.freq        = 256;
                cfg.pwm_channel = 7;
                mLight.config(cfg);
                mPanel.setLight(&mLight);
            }
            setPanel(&mPanel);
        }

    private:
        lgfx::Panel_ST7789 mPanel;
        lgfx::Bus_SPI mBus;
        lgfx::Light_PWM mLight;
};

template <class HMSYSTEM, class RADIO>
class CardputerScreen {
    public:
        void setup(IApp *app, HMSYSTEM *sys, RADIO *nrf, settings_t *cfg) {
            mApp = app;
            mSys = sys;
            mNrf = nrf;
            mCfg = cfg;

            pinMode(CARD_BTN_PIN, INPUT_PULLUP);

            mLcd.init();
            mLcd.setRotation(1);
            mLcd.setBrightness(mBrightness[mBrightIdx]);
            mLcd.fillScreen(TFT_BLACK);

            // full frame buffer avoids flicker, fall back to 8 bit or direct drawing if RAM is short
            mSpr.setColorDepth(16);
            if(nullptr == mSpr.createSprite(CARD_SCR_W, CARD_SCR_H)) {
                mSpr.setColorDepth(8);
                mSpr.createSprite(CARD_SCR_W, CARD_SCR_H);
            }
            mHasSprite = (nullptr != mSpr.getBuffer());
            DPRINT(DBG_INFO, F("Cardputer screen, sprite: "));
            DBGPRINTLN(mHasSprite ? String(mSpr.getColorDepth()) : String(F("none")));

            draw();
        }

        void loop() {
            // G0 short press: next page, long press (>1s): cycle brightness
            bool pressed = (LOW == digitalRead(CARD_BTN_PIN));
            uint32_t now = millis();
            if(pressed && !mBtnDown) {
                mBtnDown = true;
                mBtnTs = now;
                mLongDone = false;
            } else if(pressed && mBtnDown && !mLongDone && ((now - mBtnTs) > 1000)) {
                mLongDone = true;
                mBrightIdx = (mBrightIdx + 1) % 4;
                mLcd.setBrightness(mBrightness[mBrightIdx]);
            } else if(!pressed && mBtnDown) {
                mBtnDown = false;
                if(!mLongDone && ((now - mBtnTs) > 30)) {
                    mPage = (mPage + 1) % CARD_NUM_PAGES;
                    draw();
                }
            }
        }

        void tickSecond() {
            draw();
        }

    private:
        lgfx::LovyanGFX *canvas() {
            return mHasSprite ? static_cast<lgfx::LovyanGFX*>(&mSpr) : static_cast<lgfx::LovyanGFX*>(&mLcd);
        }

        void draw() {
            lgfx::LovyanGFX *g = canvas();
            g->startWrite();
            g->fillScreen(TFT_BLACK);
            drawHeader(g);
            if(0 == mPage)
                drawLive(g);
            else
                drawInfo(g);
            g->endWrite();
            if(mHasSprite)
                mSpr.pushSprite(&mLcd, 0, 0);
        }

        Inverter<> *firstInverter() {
            for(uint8_t i = 0; i < mSys->getNumInverters(); i++) {
                Inverter<> *iv = mSys->getInverterByPos(i);
                if(nullptr != iv)
                    return iv;
            }
            return nullptr;
        }

        void drawHeader(lgfx::LovyanGFX *g) {
            g->fillRect(0, 0, CARD_SCR_W, 16, 0x18E3);
            g->setFont(&fonts::Font2);
            g->setTextColor(TFT_WHITE);
            g->setTextDatum(textdatum_t::middle_left);
            Inverter<> *iv = firstInverter();
            g->drawString((nullptr != iv) ? iv->config->name : "AhoyDTU", 4, 8);

            // radio + network indicators
            bool nrfOk = mApp->getNrfEnabled() && mNrf->isChipConnected();
            g->setTextDatum(textdatum_t::middle_right);
            g->setTextColor(nrfOk ? TFT_GREEN : TFT_RED);
            g->drawString("RF", 236, 8);
            g->setTextColor(mApp->isNetworkConnected() ? TFT_GREEN : TFT_ORANGE);
            g->drawString("WiFi", 214, 8);
            g->setTextColor(TFT_DARKGREY);
            g->drawString(String(mPage + 1) + "/" + String(CARD_NUM_PAGES), 176, 8);
        }

        void drawLive(lgfx::LovyanGFX *g) {
            Inverter<> *iv = firstInverter();
            if(nullptr == iv) {
                drawMessage(g, TFT_ORANGE, "No inverter configured", "add it in the web UI");
                return;
            }
            if(!(mApp->getNrfEnabled() && mNrf->isChipConnected())) {
                drawMessage(g, TFT_RED, "nRF24 not detected", "check EXT header wiring");
                drawFooter(g);
                return;
            }

            record_t<> *rec = iv->getRecordStruct(RealTimeRunData_Debug);
            bool avail = iv->isAvailable();
            bool producing = iv->isProducing();

            // total AC power, big
            float pac = avail ? iv->getChannelFieldValue(CH0, FLD_PAC, rec) : 0.0f;
            g->setTextDatum(textdatum_t::baseline_right);
            g->setFont(&fonts::Font7);
            g->setTextColor(avail ? (producing ? TFT_YELLOW : TFT_LIGHTGREY) : TFT_DARKGREY);
            g->drawString(avail ? String((int)(pac + 0.5f)) : String("---"), 186, 70);
            g->setFont(&fonts::Font4);
            g->setTextDatum(textdatum_t::baseline_left);
            g->drawString("W", 192, 70);

            // yield
            g->setFont(&fonts::Font2);
            g->setTextColor(TFT_CYAN);
            g->setTextDatum(textdatum_t::top_left);
            float yd = iv->getChannelFieldValue(CH0, FLD_YD, rec);
            float yt = iv->getChannelFieldValue(CH0, FLD_YT, rec);
            g->drawString("Today " + String(yd / 1000.0f, 2) + " kWh", 4, 76);
            g->setTextDatum(textdatum_t::top_right);
            g->drawString("Total " + String(yt, 1) + " kWh", 236, 76);

            // per panel (DC input) bars
            uint8_t chs = iv->channels;
            if(chs > 4) chs = 4;
            if(chs > 0) {
                const int16_t top = 94, barH = 20, gap = 4;
                int16_t w = (CARD_SCR_W - 8 - (chs - 1) * gap) / chs;
                for(uint8_t ch = 1; ch <= chs; ch++) {
                    int16_t x = 4 + (ch - 1) * (w + gap);
                    float pdc = avail ? iv->getChannelFieldValue(ch, FLD_PDC, rec) : 0.0f;
                    uint16_t maxP = iv->config->chMaxPwr[ch - 1];
                    if(0 == maxP) maxP = 380;
                    int16_t fill = (int16_t)((pdc / maxP) * (w - 2));
                    if(fill > (w - 2)) fill = w - 2;
                    if(fill < 0) fill = 0;
                    g->drawRect(x, top, w, barH, TFT_DARKGREY);
                    g->fillRect(x + 1, top + 1, fill, barH - 2, 0x2C4A);
                    g->setTextColor(TFT_WHITE);
                    g->setTextDatum(textdatum_t::middle_center);
                    g->drawString("P" + String(ch) + " " + String((int)(pdc + 0.5f)) + "W", x + w / 2, top + barH / 2);
                }
            }

            drawFooter(g);
        }

        void drawFooter(lgfx::LovyanGFX *g) {
            Inverter<> *iv = firstInverter();
            String txt;
            uint16_t col = TFT_LIGHTGREY;
            if(nullptr != iv) {
                record_t<> *rec = iv->getRecordStruct(RealTimeRunData_Debug);
                uint32_t ts = rec->ts;
                uint32_t now = mApp->getTimestamp();
                if(!iv->config->enabled) {
                    txt = "inverter disabled";
                    col = TFT_ORANGE;
                } else if(!iv->commEnabled) {
                    txt = "night pause (no polling)";
                    col = TFT_DARKGREY;
                } else if(0 == ts) {
                    txt = "polling... no reply yet  tx " + String(iv->radioStatistics.txCnt);
                    col = TFT_ORANGE;
                } else {
                    uint32_t age = (now > ts) ? (now - ts) : 0;
                    txt = iv->isProducing() ? "producing" : (iv->isAvailable() ? "online, idle" : "offline");
                    txt += "  upd " + ageStr(age) + " ago";
                    col = iv->isAvailable() ? TFT_GREEN : TFT_ORANGE;
                }
            }
            g->setFont(&fonts::Font0);
            g->setTextDatum(textdatum_t::bottom_left);
            g->setTextColor(col);
            g->drawString(txt, 4, CARD_SCR_H - 2);
        }

        void drawInfo(lgfx::LovyanGFX *g) {
            g->setFont(&fonts::Font2);
            g->setTextDatum(textdatum_t::top_left);
            int16_t y = 20;
            const int16_t lh = 16;

            // network
            g->setTextColor(TFT_CYAN);
            if(mApp->isNetworkConnected()) {
                g->drawString("WiFi " + String(mCfg->sys.stationSsid) + " " + String(WiFi.RSSI()) + "dBm", 4, y); y += lh;
                g->setTextColor(TFT_WHITE);
                g->drawString("http://" + mApp->getIp(), 4, y); y += lh;
            } else if(WiFi.getMode() & WIFI_AP) {
                g->drawString("AP " WIFI_AP_SSID "  pw " + String(mCfg->sys.apPwd), 4, y); y += lh;
                g->setTextColor(TFT_WHITE);
                g->drawString("http://" + WiFi.softAPIP().toString(), 4, y); y += lh;
            } else {
                g->drawString("WiFi connecting...", 4, y); y += lh * 2;
            }

            Inverter<> *iv = firstInverter();
            if(nullptr == iv)
                return;

            record_t<> *rec = iv->getRecordStruct(RealTimeRunData_Debug);
            char sn[13];
            snprintf(sn, sizeof(sn), "%012llx", (unsigned long long)iv->config->serial.u64);
            g->setTextColor(TFT_LIGHTGREY);
            g->drawString("SN " + String(sn) + (IV_MI == iv->ivGen ? " (MI)" : " (HM)"), 4, y); y += lh;

            if(iv->isAvailable()) {
                g->drawString(String(iv->getChannelFieldValue(CH0, FLD_UAC, rec), 1) + "V  "
                    + String(iv->getChannelFieldValue(CH0, FLD_F, rec), 2) + "Hz  "
                    + String(iv->getChannelFieldValue(CH0, FLD_T, rec), 1) + "C", 4, y);
            } else
                g->drawString("no live data", 4, y);
            y += lh;

            statistics_t *st = &iv->radioStatistics;
            g->drawString("RF tx " + String(st->txCnt) + " ok " + String(st->rxSuccess)
                + " fail " + String(st->rxFail) + " none " + String(st->rxFailNoAnswer), 4, y); y += lh;

            g->setTextColor(TFT_DARKGREY);
            g->drawString(String(F("Ahoy ")) + mApp->getVersion() + "  heap " + String(ESP.getFreeHeap() / 1024) + "k", 4, y);
        }

        void drawMessage(lgfx::LovyanGFX *g, uint16_t col, const char *l1, const char *l2) {
            g->setFont(&fonts::Font4);
            g->setTextDatum(textdatum_t::middle_center);
            g->setTextColor(col);
            g->drawString(l1, CARD_SCR_W / 2, 55);
            g->setFont(&fonts::Font2);
            g->setTextColor(TFT_LIGHTGREY);
            g->drawString(l2, CARD_SCR_W / 2, 85);
        }

        String ageStr(uint32_t s) {
            if(s < 120) return String(s) + "s";
            if(s < 7200) return String(s / 60) + "m";
            return String(s / 3600) + "h";
        }

        IApp *mApp = nullptr;
        HMSYSTEM *mSys = nullptr;
        RADIO *mNrf = nullptr;
        settings_t *mCfg = nullptr;

        CardputerLgfx mLcd;
        LGFX_Sprite mSpr = LGFX_Sprite(&mLcd);
        bool mHasSprite = false;

        uint8_t mPage = 0;
        const uint8_t mBrightness[4] = {128, 255, 40, 0};
        uint8_t mBrightIdx = 0;
        bool mBtnDown = false;
        bool mLongDone = false;
        uint32_t mBtnTs = 0;
};

#endif /*CARDPUTER_ADV*/

#endif /*__CARDPUTER_SCREEN_H__*/
