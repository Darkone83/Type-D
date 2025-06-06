#include "parser_xboxsmbus.h"
#include "cache_manager.h"
#include <Arduino.h>
#include <string.h>

// Known Xbox SMBus 7-bit addresses
constexpr uint8_t ADDR_SMC      = 0x10;
constexpr uint8_t ADDR_EEPROM   = 0x54;
constexpr uint8_t ADDR_TEMP     = 0x4C;

// App codes per Xbox wiki and reverse engineering
static const char* appNameFromCode(uint8_t code) {
    switch (code) {
        case 0: return "Dashboard";
        case 1: return "Game";
        case 2: return "DVD";
        case 3: return "AudioCD";
        case 4: return "Update";
        case 5: return "LiveDash";
        case 6: return "Linux";
        default: return "Unknown";
    }
}

// Parser state
struct SMBusSession {
    uint8_t address = 0;
    bool isRead = false;
    uint8_t reg = 0;
    bool regValid = false;
    bool inTransaction = false;
    // EEPROM capture state
    bool eepromMacCapture = false;
    uint8_t eepromMacReg = 0;
    uint8_t eepromMacBuf[6];
    uint8_t eepromMacBufLen = 0;
    // IP capture state
    bool eepromIpCapture = false;
    uint8_t eepromIpReg = 0;
    uint8_t eepromIpBuf[4];
    uint8_t eepromIpBufLen = 0;
    uint32_t lastEventMillis = 0;
};

static SMBusSession sess = {0};

static void resetSession() {
    sess.address = 0;
    sess.isRead = false;
    sess.reg = 0;
    sess.regValid = false;
    sess.inTransaction = false;
    sess.eepromMacCapture = false;
    sess.eepromMacBufLen = 0;
    sess.eepromIpCapture = false;
    sess.eepromIpBufLen = 0;
    sess.lastEventMillis = millis();
}

void Parser_XboxSMBus::parse(const I2C_Event &evt) {
    if (evt.type == I2C_Event::START) resetSession();

    if (evt.type == I2C_Event::ADDRESS) {
        sess.address = (evt.value >> 1) & 0x7F;
        sess.isRead = (evt.value & 1);
        sess.inTransaction = true;
        sess.regValid = false;
    }

    if (evt.type == I2C_Event::DATA && sess.inTransaction) {
        // ADM1032 (0x4C): [reg][data]
        if (sess.address == ADDR_TEMP) {
            if (!sess.regValid) {
                sess.reg = evt.value;
                sess.regValid = true;
            } else {
                if (sess.reg == 0x00) {
                    Cache_Manager::updateAmbientTemp((int8_t)evt.value);
                } else if (sess.reg == 0x01) {
                    Cache_Manager::updateCpuTemp((int8_t)evt.value);
                }
                sess.regValid = false;
            }
        }
        // SMC (0x10): [reg][data]
        else if (sess.address == ADDR_SMC) {
            if (!sess.regValid) {
                sess.reg = evt.value;
                sess.regValid = true;
            } else {
                switch (sess.reg) {
                    case 0x20:
                        Cache_Manager::updateFan(evt.value);
                        break;
                    case 0x21:
                        // Fan mode (optional)
                        break;
                    case 0x24:
                        // Power LED (optional)
                        break;
                    case 0x2C:
                        Cache_Manager::updateCpuTemp((int8_t)evt.value);
                        break;
                    case 0x2D:
                        Cache_Manager::updateAmbientTemp((int8_t)evt.value);
                        break;
                    case 0x30:
                        Cache_Manager::updateApp(appNameFromCode(evt.value));
                        break;
                    default:
                        break;
                }
                sess.regValid = false;
            }
        }
        // EEPROM (0x54): [reg][data]
        else if (sess.address == ADDR_EEPROM) {
            if (!sess.regValid) {
                sess.reg = evt.value;
                sess.regValid = true;
                // Prepare for MAC capture
                if (sess.reg == 0x14) {
                    sess.eepromMacCapture = true;
                    sess.eepromMacBufLen = 0;
                    sess.eepromMacReg = 0x14;
                }
                // Prepare for IP capture
                if (sess.reg == 0x6A) {
                    sess.eepromIpCapture = true;
                    sess.eepromIpBufLen = 0;
                    sess.eepromIpReg = 0x6A;
                }
            } else {
                // MAC
                if (sess.eepromMacCapture && sess.eepromMacReg >= 0x14 && sess.eepromMacReg <= 0x19) {
                    sess.eepromMacBuf[sess.eepromMacBufLen++] = evt.value;
                    sess.eepromMacReg++;
                    if (sess.eepromMacBufLen == 6) {
                        char mac[18];
                        snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
                            sess.eepromMacBuf[0], sess.eepromMacBuf[1], sess.eepromMacBuf[2],
                            sess.eepromMacBuf[3], sess.eepromMacBuf[4], sess.eepromMacBuf[5]);
                        Cache_Manager::updateMac(mac);
                        sess.eepromMacCapture = false;
                    }
                }
                // IP
                if (sess.eepromIpCapture && sess.eepromIpReg >= 0x6A && sess.eepromIpReg <= 0x6D) {
                    sess.eepromIpBuf[sess.eepromIpBufLen++] = evt.value;
                    sess.eepromIpReg++;
                    if (sess.eepromIpBufLen == 4) {
                        char ip[16];
                        snprintf(ip, sizeof(ip), "%u.%u.%u.%u",
                            sess.eepromIpBuf[0], sess.eepromIpBuf[1], sess.eepromIpBuf[2], sess.eepromIpBuf[3]);
                        Cache_Manager::updateIp(ip);
                        sess.eepromIpCapture = false;
                    }
                }
                sess.regValid = false;
            }
        }
    }

    if (evt.type == I2C_Event::STOP) resetSession();
    if (sess.inTransaction && millis() - sess.lastEventMillis > 100) resetSession();
}

void Parser_XboxSMBus::printStatus() {
    const XboxStatus &st = Cache_Manager::getStatus();
    Serial.printf("[STATUS] CPU: %dC | Ambient: %dC | Fan: %d | App: %s | MAC: %s | IP: %s\n",
        st.cpuTemp, st.ambientTemp, st.fanSpeed, st.currentApp, st.macAddress, st.ipAddress);
}
