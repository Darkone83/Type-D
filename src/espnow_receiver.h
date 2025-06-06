#pragma once

#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>

// Struct to match XboxStatus telemetry packet
struct XboxStatus {
    int fanSpeed = -1;
    int cpuTemp = -1000;
    int ambientTemp = -1000;
    char currentApp[32] = "";
    char ipAddress[16] = "";
    char macAddress[18] = "";
};

namespace ESPNOWReceiver {

// Start ESPNOW in receive mode (call once in setup)
void begin();

// Stop ESPNOW (optional)
void end();

// Returns true if a new XboxStatus packet was received since last getLatest()
bool hasPacket();

// Get the latest received packet (resets hasPacket() to false)
XboxStatus getLatest();

// Optional: register a callback for new packets
void onPacket(void (*cb)(const XboxStatus&));

} // namespace ESPNOWReceiver
