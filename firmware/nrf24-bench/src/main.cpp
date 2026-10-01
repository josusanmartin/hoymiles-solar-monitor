// nRF24 bench test: self-test, energy scan and constant carrier
// serial commands: p = probe, s = scan, c<ch> = constant carrier on channel, x = stop carrier
#include <Arduino.h>
#include <SPI.h>
#include <RF24.h>

#define PIN_CE  4
#define PIN_CSN 5

RF24 radio(PIN_CE, PIN_CSN);
bool carrierOn = false;

uint8_t readReg(uint8_t reg) {
    SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
    digitalWrite(PIN_CSN, LOW);
    SPI.transfer(reg & 0x1f);
    uint8_t v = SPI.transfer(0xff);
    digitalWrite(PIN_CSN, HIGH);
    SPI.endTransaction();
    return v;
}

void probe() {
    bool ok = radio.isChipConnected();
    Serial.printf("PROBE connected=%d pVariant=%d CONFIG=%02X SETUP_AW=%02X RF_SETUP=%02X STATUS=%02X\n",
        ok, radio.isPVariant(), readReg(0x00), readReg(0x03), readReg(0x06), readReg(0x07));
}

void scan(uint8_t passes) {
    uint8_t hits[126] = {0};
    if(carrierOn) { radio.stopConstCarrier(); carrierOn = false; }
    radio.setAutoAck(false);
    for(uint8_t p = 0; p < passes; p++) {
        for(uint8_t ch = 0; ch < 126; ch++) {
            radio.setChannel(ch);
            radio.startListening();
            delayMicroseconds(300);
            bool hit = radio.testRPD();
            radio.stopListening();
            hit |= radio.testRPD();
            if(hit && hits[ch] < 255) hits[ch]++;
        }
    }
    Serial.print("SCAN ");
    for(uint8_t ch = 0; ch < 126; ch++) {
        Serial.print(hits[ch]);
        Serial.print(ch < 125 ? "," : "\n");
    }
}

void carrier(uint8_t ch) {
    radio.stopListening();
    radio.startConstCarrier(RF24_PA_MAX, ch);
    carrierOn = true;
    Serial.printf("CARRIER on ch %u (%u MHz)\n", ch, 2400 + ch);
}

void setup() {
    Serial.begin(115200);
    delay(500);
    if(!radio.begin())
        Serial.println("BEGIN failed");
    // the module stays powered across ESP32 resets: clear a leftover constant-carrier mode
    radio.stopConstCarrier();
    radio.powerUp();
    radio.setAutoAck(true);
    radio.enableDynamicPayloads();
    radio.setCRCLength(RF24_CRC_16);
    radio.setDataRate(RF24_250KBPS);
    radio.setPALevel(RF24_PA_MAX);
    probe();
    scan(40);
    Serial.println("READY");
}

void loop() {
    if(!Serial.available()) return;
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if(cmd == "p") probe();
    else if(cmd == "s") scan(40);
    else if(cmd.startsWith("c")) carrier(cmd.substring(1).toInt());
    else if(cmd == "x") { radio.stopConstCarrier(); carrierOn = false; Serial.println("CARRIER off"); }
}
