#include "espnow_receiver.h"

static XboxPacket latestPacket;
static bool hasPacketFlag = false;

// Correct ESP-NOW receive callback for ESP32 Arduino Core 3.x+
void onDataRecv(const esp_now_recv_info_t* recv_info, const uint8_t* data, int len) {
    Serial.printf("[ESPNOW] Packet received from " MACSTR " (len=%d)\n", 
        MAC2STR(recv_info->src_addr), len);

    if (len == sizeof(XboxPacket)) {
        memcpy(&latestPacket, data, sizeof(XboxPacket));
        hasPacketFlag = true;
        Serial.println("[ESPNOW] XboxPacket parsed and ready.");
    } else {
        Serial.printf("[ESPNOW] Warning: Received packet of unexpected size (%d bytes)\n", len);
    }
}


void ESPNOWReceiver::begin() {
    // Only set mode if not already in AP+STA
    if (WiFi.getMode() != WIFI_AP_STA) {
        WiFi.mode(WIFI_AP_STA);   // Stay compatible with AP and ESPNOW
    }
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
