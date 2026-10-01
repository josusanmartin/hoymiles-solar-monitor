# AhoyDTU on M5Stack Cardputer ADV

Build env `cardputer-adv` (see `src/platformio.ini`). The Cardputer runs AhoyDTU as the DTU
for a Hoymiles MI/HM inverter and shows live data on its screen. Set `SOLAR_INVERTER_SERIAL` at build time
to preconfigure the inverter, or add it in the web UI.

## Wiring: nRF24L01+ PA/LNA → Cardputer ADV rear EXT 2.54-14P header

| nRF24 | EXT pin | Cardputer GPIO |
|---|---|---|
| SCK  | 7  | G40 |
| MOSI | 9  | G14 |
| MISO | 11 | G39 |
| CE   | 3  | G4  |
| CSN  | 5  | G6  |
| IRQ  | 13 | G5  |
| GND  | 4  | GND |
| VCC  | 6 (5VOUT) | → **5 V input of the nRF24 regulator adapter**, never the bare module |

The header has no 3.3 V pin. Use the 5V→3.3V nRF24 adapter board. Check pin 1 against the
silkscreen / M5 docs before powering, and attach the antenna before the radio transmits.
The SPI bus is shared with the microSD slot, so leave the SD card out.
The nRF pins are compiled in and override saved settings (src/config/settings.h).
`/api/system` → `radioNrf.probe` shows raw nRF register reads for wiring checks.

## Screen

- Page 1: AC power (W), today's yield, total yield, a bar for each of the 4 panel inputs, and polling status.
- Page 2: Wi-Fi / web UI address, inverter serial, AC V / Hz / temperature, RF counters.
- **G0 button** (side): short press switches the page; hold >1 s cycles brightness (50%, 100%, 15%, off).
- Header `RF` turns green once the nRF24 is detected. `WiFi` turns green once it joins your network.

## First setup

1. Join Wi-Fi `AHOY-DTU` (password `esp_8266`) and open http://192.168.4.1
2. System → Settings → Network: enter your home Wi-Fi and save. After the reboot, page 2 shows the new IP.
3. Settings → Location: set latitude/longitude and timezone (defaults are Germany).
   Only matters if you enable "pause at night".
4. The inverter is already added. It polls without NTP time (`startWithoutTime`).

## Build / flash / restore

```
cd src
~/.platformio-venv/bin/pio run -e cardputer-adv -t upload --upload-port <port>
# restore the original Cardputer firmware:
esptool --port <port> write-flash 0 <your-backup>.bin   # make one first with: esptool --port <port> read-flash 0 0x800000 <your-backup>.bin
```

Note: an inverter only reports **production** (PV → AC), not household consumption.
