#include "debug_logger.h"
#include <Arduino.h>

// === Replace these with your actual struct definitions if needed ===
struct I2C_Event {
    enum Type {
        START,
        STOP,
        ADDRESS,
        DATA,
        ACK,
        NACK
    } type;
    uint8_t value;
    bool isRead;
};

struct XboxStatus {
    int fanSpeed;
    int cpuTemp;
    int ambientTemp;
    char currentApp[16];
    char macAddress[18];
    char ipAddress[16];
};
// === End struct definitions ===

// SMBus addresses of interest (7-bit)
constexpr uint8_t ADDR_SMC    = 0x10;
constexpr uint8_t ADDR_EEPROM = 0x54;
constexpr uint8_t ADDR_TEMP   = 0x4C;

void Debug_Logger::logI2CEvent(const I2C_Event &evt) {
    static uint8_t lastAddress7 = 0;
    static bool lastRelevant = false;

    if (evt.type == I2C_Event::ADDRESS) {
        uint8_t addr7 = evt.value >> 1;
        lastAddress7 = addr7;
        lastRelevant = (addr7 == ADDR_SMC) || (addr7 == ADDR_EEPROM) || (addr7 == ADDR_TEMP);
        if (lastRelevant) {
            Serial.printf("[I2C] ADDRESS: 0x%02X (%s) (7-bit: 0x%02X)\n",
                evt.value, evt.isRead ? "READ" : "WRITE", addr7);
        }
    } else if (lastRelevant) {
        switch (evt.type) {
            case I2C_Event::DATA:
                Serial.printf("[I2C] DATA: 0x%02X\n", evt.value);
                break;
            case I2C_Event::ACK:
                Serial.println("[I2C] ACK");
                break;
            case I2C_Event::NACK:
                Serial.println("[I2C] NACK");
                break;
            case I2C_Event::START:
                Serial.println("[I2C] START");
                break;
            case I2C_Event::STOP:
                Serial.println("[I2C] STOP");
                break;
            default:
                break;
        }
    }
    if (evt.type == I2C_Event::STOP) {
        lastRelevant = false;
        lastAddress7 = 0;
    }
}

void Debug_Logger::logStatusTransmission(const XboxStatus &status) {
    Serial.printf("[ESP-NOW SEND] Fan: %d | CPU: %d | Ambient: %d | App: %s | MAC: %s | IP: %s\n",
                  status.fanSpeed, status.cpuTemp, status.ambientTemp,
                  status.currentApp, status.macAddress, status.ipAddress);
}
