#pragma once
#include "disp_cfg.h"

// --- Status structure for UDP/packet sending/receiving ---
// Mirrors the wire formats used by Type-D core (50504) and expansion (50505).
struct XboxStatus {
    // Core (50504)
    int fanSpeed     = -1;       // 0–100%
    int cpuTemp      = -1000;    // Celsius
    int ambientTemp  = -1000;    // Celsius
    char currentApp[32] = {0};   // App name

    // Expansion (50505)
    int trayState    = -1;       // Tray state
    int avPack       = -1;       // AV pack ID
    int picVersion   = -1;       // PIC firmware version
    int xboxVersion  = -1;       // Xbox hardware version
    int encoder      = -1;       // Video encoder type
    int videoWidth   = -1;       // Active video width
    int videoHeight  = -1;       // Active video height

    // Always set in udp_detect.cpp using videoWidth/videoHeight
    char resolution[32] = {0};   // e.g. "1280x720 (720p)"
};

namespace xbox_status {
    void show(LGFX* tft, const XboxStatus& packet);
}
