#include "xbox_smbus_poll.h"
#include "parser_xboxsmbus.h"
#include "cache_manager.h"
#include "debug_logger.h"
#include "wifimgr.h"
#include "udp_stat.h"
#include "led_stat.h"
#include "smbus_ext.h"
#include <Wire.h>

#define I2C_SDA_PIN 7   // Set to your working SMBus SDA pin
#define I2C_SCL_PIN 6   // Set to your working SMBus SCL pin

XboxSMBusStatus smbusStatus;

void setup() {
    LedStat::begin();
    LedStat::setStatus(LedStatus::Booting);

    Serial.begin(115200);
    delay(200);  // Give time for Serial to initialize

    WiFiMgr::begin();
    Cache_Manager::begin();
    XboxSMBusPoll::begin(I2C_SDA_PIN, I2C_SCL_PIN);
    SMBusExt::begin();

    if (WiFiMgr::isConnected()) {
        UDPStat::begin();
    }

    Serial.println("Xbox SMBus ESP32 sniffer started.");
}

void loop() {
    LedStat::loop();
    WiFiMgr::loop();
    
    Cache_Manager::pollTitleUdp();

    // 1. SMBus Polling (every 1s)
    static unsigned long lastPoll = 0;
    if (millis() - lastPoll > 1000) {
        lastPoll = millis();
        if (XboxSMBusPoll::poll(smbusStatus)) {
            Cache_Manager::updateFromSmbus(smbusStatus);

            // --- poll and send EXT status (trayState, avPackState, picVer)
            SMBusExt::sendExtStatus();
        }
    }

    // 2. Status Debug Print (every 5s)
    static unsigned long lastPrint = 0;
    if (millis() - lastPrint > 5000) {
        lastPrint = millis();
        const XboxStatus& st = Cache_Manager::getStatus();
        Serial.printf("[SMBus] Fan=%d%%, CPU=%dC, Ambient=%dC, App='%s'\n",
            st.fanSpeed, st.cpuTemp, st.ambientTemp, st.currentApp);
    }

    // 3. UDP Packet Sending (as fast as udp_stat wants)
    if (WiFiMgr::isConnected()) {
        UDPStat::loop();
    }
}
