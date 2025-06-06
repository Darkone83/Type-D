// espnow_transmitter.cpp
#include "espnow_transmitter.h"
#include "cache_manager.h"
#include <WiFi.h>
#include <esp_now.h>
#include <Arduino.h>

static unsigned long lastSent = 0;
static const unsigned long SEND_INTERVAL_MS = 1000; // Adjust as needed

// ESPNOW data payload
struct ESPNOWPacket {
    XboxStatus status;
};

// The ESP-NOW broadcast MAC address
static const uint8_t broadcastAddr[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    // Optionally handle sent status
}

void ESPNow_Transmitter::begin() {
    WiFi.mode(WIFI_STA);
    if (esp_now_init() != ESP_OK) {
        Serial.println("ESP-NOW init failed");
        return;
    }
    esp_now_register_send_cb(OnDataSent);
    Serial.println("ESP-NOW transmitter started (broadcast mode).");
}

void ESPNow_Transmitter::loop() {
    if (millis() - lastSent >= SEND_INTERVAL_MS) {
        lastSent = millis();
        ESPNOWPacket packet;
        packet.status = Cache_Manager::getStatus();
        esp_err_t result = esp_now_send(broadcastAddr, (uint8_t*)&packet, sizeof(packet));
        if (result == ESP_OK) {
            Serial.println("[ESP-NOW] Broadcast data sent!");
        } else {
            Serial.printf("[ESP-NOW] Broadcast send failed (%d)\n", result);
        }
    }
}
