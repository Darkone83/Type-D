#include "cache_manager.h"
#include "xbox_smbus_poll.h"
#include <cstring>

static XboxStatus cache;

void Cache_Manager::begin() {
    reset();
}

void Cache_Manager::reset() {
    cache.fanSpeed = -1;
    cache.cpuTemp = -1000;
    cache.ambientTemp = -1000;
    memset(cache.currentApp, 0, sizeof(cache.currentApp));
}

void Cache_Manager::setFanSpeed(int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    cache.fanSpeed = percent;
    // Debug:
    Serial.printf("[CacheMgr] Fan speed updated: %d%%\n", percent);
}

void Cache_Manager::setCpuTemp(int celsius) {
    // Accept only valid range for Xbox (avoid garbage)
    if (celsius > 0 && celsius < 100) {
        cache.cpuTemp = celsius;
        Serial.printf("[CacheMgr] CPU Temp updated: %dC\n", celsius);
    }
}

void Cache_Manager::setAmbientTemp(int celsius) {
    if (celsius > 0 && celsius < 100) {
        cache.ambientTemp = celsius;
        Serial.printf("[CacheMgr] Ambient Temp updated: %dC\n", celsius);
    }
}

void Cache_Manager::setCurrentApp(const char *name) {
    if (name && *name) {
        strncpy(cache.currentApp, name, sizeof(cache.currentApp) - 1);
        cache.currentApp[sizeof(cache.currentApp) - 1] = 0;
        Serial.printf("[CacheMgr] App name updated: %s\n", cache.currentApp);
    }
}

// --- PATCH: update cache from SMBus struct ---
void Cache_Manager::updateFromSmbus(const XboxSMBusStatus& st) {
    setFanSpeed(st.fanSpeed);
    setCpuTemp(st.cpuTemp);
    setAmbientTemp(st.boardTemp); // or st.ambientTemp, if that's your struct field
    // setCurrentApp(st.app); // Only if app name is available in st
}

const XboxStatus& Cache_Manager::getStatus() {
    return cache;
}
