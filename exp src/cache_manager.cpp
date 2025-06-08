#include "cache_manager.h"
#include <Arduino.h>
#include <string.h>

static XboxStatus status;

void Cache_Manager::begin() { clear(); }

void Cache_Manager::clear() {
    status.cpuTemp = -1000;
    status.ambientTemp = -1000;
    status.fanSpeed = -1;
    memset(status.currentApp, 0, sizeof(status.currentApp));
    status.cpuTempUpdated = 0;
    status.ambientTempUpdated = 0;
    status.fanSpeedUpdated = 0;
    status.appUpdated = 0;
}

void Cache_Manager::updateCpuTemp(int tempC) {
    if (tempC > 10 && tempC < 80) { // Filter for valid temp range
        status.cpuTemp = tempC;
        status.cpuTempUpdated = millis();
    }
}

void Cache_Manager::updateAmbientTemp(int tempC) {
    if (tempC > 0 && tempC < 60) { // Filter for valid ambient range
        status.ambientTemp = tempC;
        status.ambientTempUpdated = millis();
    }
}

void Cache_Manager::updateFan(int fanPct) {
    // Accept 0–100 as direct percentage, or 0–255 scaled
    if (fanPct >= 0 && fanPct <= 255) {
        int scaled = (fanPct > 100) ? (fanPct * 100 / 255) : fanPct;
        if (scaled >= 0 && scaled <= 100) {
            status.fanSpeed = scaled;
            status.fanSpeedUpdated = millis();
        }
    }
}

void Cache_Manager::updateApp(const char* app) {
    strncpy(status.currentApp, app, sizeof(status.currentApp) - 1);
    status.currentApp[sizeof(status.currentApp) - 1] = '\0';
    status.appUpdated = millis();
}

const XboxStatus& Cache_Manager::getStatus() { return status; }
