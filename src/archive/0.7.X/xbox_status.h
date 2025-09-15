#pragma once
#include "disp_cfg.h"

// --- Status structure for UDP/packet sending/receiving ---
struct XboxStatus {
    int fanSpeed = -1;          // 0–100%
    int cpuTemp = -1000;        // Celsius
    int ambientTemp = -1000;    // Celsius
    char currentApp[32] = {0};  // App name (optional)
};

namespace xbox_status {
    void show(LGFX* tft, const XboxStatus& packet);
}
