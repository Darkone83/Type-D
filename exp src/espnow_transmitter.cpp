#include "espnow_transmitter.h"
#include "cache_manager.h"
#include <esp_now.h>
#include <WiFi.h>

static uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

void ESPNow_Transmitter::begin() {
    WiFi.mode(WIFI_STA);
    if (esp_now_init() != ESP_OK) {
        Serial.println("[ESP-NOW] Init failed");
        return;
    }
}

void ESPNow_Transmitter::loop() {
    static unsigned long lastSend = 0;
    if (millis() - lastSend < 1000) return; // 1 Hz
    lastSend = millis();

    const XboxStatus &st = Cache_Manager::getStatus();

    // Construct status packet (adjust fields as needed)
    struct Packet {
        int fanSpeed;
        int cpuTemp;
        int ambientTemp;
        char app[16];
    } packet;
    packet.fanSpeed    = st.fanSpeed;
    packet.cpuTemp     = st.cpuTemp;
    packet.ambientTemp = st.ambientTemp;
    strncpy(packet.app, st.currentApp, sizeof(packet.app) - 1);
    packet.app[sizeof(packet.app) - 1] = 0;

    esp_err_t result = esp_now_send(broadcastAddress, (uint8_t*)&packet, sizeof(packet));
    Serial.printf("[ESP-NOW SEND] Fan: %d | CPU: %d | Ambient: %d | App: %s\n",
        packet.fanSpeed, packet.cpuTemp, packet.ambientTemp, packet.app);
    if (result != ESP_OK) {
        Serial.printf("[ESP-NOW] Broadcast send failed (%d)\n", result);
    }
}
