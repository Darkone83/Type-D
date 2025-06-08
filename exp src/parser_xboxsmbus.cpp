#include "parser_xboxsmbus.h"
#include "cache_manager.h"
#include <Arduino.h>
#include <string.h>

constexpr uint8_t ADDR_SMC  = 0x10;
constexpr uint8_t ADDR_TEMP = 0x4C;

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

struct SMBusSession {
    uint8_t address = 0;
    bool isRead = false;
    uint8_t reg = 0;
    bool regValid = false;
    bool inTransaction = false;
    uint8_t lastSMCReg = 0;
    bool lastSMCRegValid = false;
    uint8_t lastTempReg = 0;
    bool lastTempRegValid = false;
    uint32_t lastEventMillis = 0;
};

static SMBusSession sess = {0};

static void resetSession() {
    sess.address = 0;
    sess.isRead = false;
    sess.reg = 0;
    sess.regValid = false;
    sess.inTransaction = false;
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
        // --- SMC (0x10) ---
        if (sess.address == ADDR_SMC) {
            if (sess.isRead && sess.lastSMCRegValid) {
                switch (sess.lastSMCReg) {
                    case 0x20: // Fan (read)
                        Cache_Manager::updateFan(evt.value);
                        break;
                    case 0x2C: // CPU temp
                        Cache_Manager::updateCpuTemp((int8_t)evt.value);
                        break;
                    case 0x2D: // Ambient temp
                        Cache_Manager::updateAmbientTemp((int8_t)evt.value);
                        break;
                    case 0x30: // App
                        Cache_Manager::updateApp(appNameFromCode(evt.value));
                        break;
                    default: break;
                }
            } else if (!sess.regValid) {
                sess.reg = evt.value;
                sess.regValid = true;
                sess.lastSMCReg = evt.value;
                sess.lastSMCRegValid = true;
            }
            // Accept write data for FAN only (directly after reg=0x20)
            else if (sess.regValid && sess.lastSMCReg == 0x20) {
                Cache_Manager::updateFan(evt.value);
                sess.regValid = false;
            }
        }
        // --- Temp sensor (0x4C) ---
        else if (sess.address == ADDR_TEMP) {
            if (sess.isRead && sess.lastTempRegValid) {
                switch (sess.lastTempReg) {
                    case 0x00: Cache_Manager::updateAmbientTemp((int8_t)evt.value); break;
                    case 0x01: Cache_Manager::updateCpuTemp((int8_t)evt.value); break;
                    default: break;
                }
            } else if (!sess.regValid) {
                sess.reg = evt.value;
                sess.regValid = true;
                sess.lastTempReg = evt.value;
                sess.lastTempRegValid = true;
            }
        }
    }

    if (evt.type == I2C_Event::STOP) resetSession();
    if (sess.inTransaction && millis() - sess.lastEventMillis > 100) resetSession();
}

void Parser_XboxSMBus::printStatus() {
    const XboxStatus &st = Cache_Manager::getStatus();
    Serial.printf("[STATUS] CPU: %sC | Ambient: %sC | Fan: %s | App: %s\n",
        (st.cpuTemp > -100 ? String(st.cpuTemp).c_str() : "N/A"),
        (st.ambientTemp > -100 ? String(st.ambientTemp).c_str() : "N/A"),
        (st.fanSpeed >= 0 ? String(st.fanSpeed).c_str() : "N/A"),
        st.currentApp[0] ? st.currentApp : "N/A"
    );
}
