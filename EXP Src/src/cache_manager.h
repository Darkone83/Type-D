#pragma once

#include <Arduino.h>

// --- Status structure for UDP/packet sending ---
struct XboxStatus {
    int fanSpeed = -1;          // 0–100%
    int cpuTemp = -1000;        // Celsius
    int ambientTemp = -1000;    // Celsius
    char currentApp[32] = {0};  // App name (optional)
};

// --- Add this forward struct declaration if needed ---
struct XboxSMBusStatus;

namespace Cache_Manager {
    void begin();

    // Setters
    void setFanSpeed(int percent);
    void setCpuTemp(int celsius);
    void setAmbientTemp(int celsius);
    void setCurrentApp(const char *name);

    // Get current snapshot
    const XboxStatus& getStatus();

    // Reset all fields to defaults
    void reset();

    // PATCH: Update all cache fields from SMBus struct
    void updateFromSmbus(const XboxSMBusStatus& st);
}
