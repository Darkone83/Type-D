#include "espnow_receiver.h"
#include <esp_now.h>

namespace ESPNOWReceiver {

static volatile bool _hasPacket = false;
static XboxStatus _lastPacket;
static void (*_userCallback)(const XboxStatus&) = nullptr;

// CORRECT ESP-NOW CALLBACK SIGNATURE FOR ESP32 Arduino 3.x+ (S3, etc)
static void onReceive(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    if (len == sizeof(XboxStatus)) {
        memcpy((void*)&_lastPacket, data, sizeof(XboxStatus));
        _hasPacket = true;
        if (_userCallback) _userCallback(_lastPacket);
    }
}

void begin() {
    WiFi.mode(WIFI_STA);
    if (esp_now_init() != ESP_OK) {
        Serial.println("ESP-NOW init failed!");
        return;
    }
    esp_now_register_recv_cb(onReceive);
}

void end() {
    esp_now_unregister_recv_cb();
    esp_now_deinit();
    _hasPacket = false;
}

bool hasPacket() {
    return _hasPacket;
}

XboxStatus getLatest() {
    _hasPacket = false;
    return _lastPacket;
}

void onPacket(void (*cb)(const XboxStatus&)) {
    _userCallback = cb;
}

} // namespace ESPNOWReceiver
