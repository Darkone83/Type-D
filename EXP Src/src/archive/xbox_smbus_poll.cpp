#include "xbox_smbus_poll.h"
#include "parser_xboxsmbus.h"
#include <Wire.h>

#define SMC_ADDRESS    0x10
#define SMC_CPUTEMP    0x09
#define SMC_BOARDTEMP  0x0A
#define SMC_FANSPEED   0x10
// Add any other needed addresses/regs

void XboxSMBusPoll::begin(uint8_t sdaPin, uint8_t sclPin) {
    Wire.begin(sdaPin, sclPin);
}

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

bool XboxSMBusPoll::poll(XboxSMBusStatus& status) {
    bool ok = true;
    uint8_t val;

    // CPU Temp
    if (readSMBusByte(SMC_ADDRESS, SMC_CPUTEMP, val) == 0 && val < 120) {
        status.cpuTemp = val;
    } else {
        ok = false;
    }

    // Board Temp (Ambient)
    if (readSMBusByte(SMC_ADDRESS, SMC_BOARDTEMP, val) == 0 && val < 120) {
        status.boardTemp = val;
    } else {
        ok = false;
    }

    // Fan Speed (raw 0-50, convert to %)
    if (readSMBusByte(SMC_ADDRESS, SMC_FANSPEED, val) == 0 && val <= 50) {
        status.fanSpeed = val * 2; // % value (0-100)
    } else {
        ok = false;
    }

    // App name parsing would need custom logic (typically from SPI, not SMBus)
    // status.app[0] = 0; // Or parse as needed

    return ok;
}
