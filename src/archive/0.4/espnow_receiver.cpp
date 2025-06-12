#include "espnow_receiver.h"

static XboxPacket latestPacket;
static bool hasPacketFlag = false;

// Correct ESP-NOW receive callback for ESP32 Arduino Core 3.x+
void onDataRecv(const esp_now_recv_info_t* recv_info, const uint8_t* data, int len) {
    if (len == sizeof(XboxPacket)) {
        memcpy(&latestPacket, data, sizeof(XboxPacket));
        hasPacketFlag = true;
    }
}

void ESPNOWReceiver::begin() {
    WiFi.mode(WIFI_STA);
    if (esp_now_init() != ESP_OK) {
        Serial.println("Error initializing ESP-NOW");
        return;
    }
    esp_now_register_recv_cb(onDataRecv);
}

bool ESPNOWReceiver::hasPacket() {
    return hasPacketFlag;
}

XboxPacket ESPNOWReceiver::getLatest() {
    return latestPacket;
}
