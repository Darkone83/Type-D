#include "smbus_ext.h"
#include <WiFiUdp.h>
#include <Wire.h>

#define SMBUS_EXT_PORT 50505
#define SMC_ADDRESS    0x10
#define SMC_TRAY       0x03
#define SMC_AVSTATE    0x04
#define SMC_VER        0x01

static WiFiUDP extUdp;

// Helper to poll a single byte from SMBus
static int readSMBusByte(uint8_t address, uint8_t reg, uint8_t& value) {
    Wire.beginTransmission(address);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return -1;
    Wire.requestFrom(address, (uint8_t)1);
    if (Wire.available()) {
        value = Wire.read();
        return 0;
    }
    return -1;
}

void SMBusExt::begin() {
    extUdp.begin(SMBUS_EXT_PORT);
}

void SMBusExt::loop() {
    // Example: poll and send every 2 seconds
    static unsigned long last = 0;
    if (millis() - last > 2000) {
        last = millis();
        sendExtStatus();
    }
}

// Poll, fill, and broadcast the ext status struct
void SMBusExt::sendExtStatus() {
    Status packet;
    uint8_t value;

    // Poll each extended field; -1 if fail
    packet.trayState   = (readSMBusByte(SMC_ADDRESS, SMC_TRAY, value) == 0) ? value : -1;
    packet.avPackState = (readSMBusByte(SMC_ADDRESS, SMC_AVSTATE, value) == 0) ? value : -1;
    packet.picVer      = (readSMBusByte(SMC_ADDRESS, SMC_VER, value) == 0) ? value : -1;

    extUdp.beginPacket("255.255.255.255", SMBUS_EXT_PORT);
    extUdp.write((const uint8_t*)&packet, sizeof(packet));
    extUdp.endPacket();

    Serial.printf("[SMBusExt] Sent EXT status: Tray=%d AV=%d Ver=%d\n",
        packet.trayState, packet.avPackState, packet.picVer);
}

void SMBusExt::sendCustomStatus(const Status& status) {
    extUdp.beginPacket("255.255.255.255", SMBUS_EXT_PORT);
    extUdp.write((const uint8_t*)&status, sizeof(status));
    extUdp.endPacket();

    Serial.printf("[SMBusExt] Sent CUSTOM EXT status: Tray=%d AV=%d Ver=%d\n",
        status.trayState, status.avPackState, status.picVer);
}
