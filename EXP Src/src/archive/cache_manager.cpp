#include "cache_manager.h"
#include "xbox_smbus_poll.h"
#include <WiFiUdp.h>
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
}

void Cache_Manager::setCpuTemp(int celsius) {
    // Accept only valid range for Xbox (avoid garbage)
    if (celsius > 0 && celsius < 100) {
        cache.cpuTemp = celsius;
    }
}

void Cache_Manager::setAmbientTemp(int celsius) {
    if (celsius > 0 && celsius < 100) {
        cache.ambientTemp = celsius;
    }
}

void Cache_Manager::setCurrentApp(const char *name) {
    if (name && *name) {
        strncpy(cache.currentApp, name, sizeof(cache.currentApp) - 1);
        cache.currentApp[sizeof(cache.currentApp) - 1] = 0;
        //Serial.printf("[CacheMgr] App name updated: %s\n", cache.currentApp);
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

static WiFiUDP g_appUdp;
static bool g_udpBound = false;

// Non-blocking single-shot read helper
static bool recv_line_udp(WiFiUDP& u, char* buf, size_t buflen) {
    int pktLen = u.parsePacket();
    if (pktLen <= 0) return false;
    int n = u.read(buf, buflen - 1);
    if (n < 0) return false;
    buf[n] = 0;
    return true;
}

// Very tolerant parser: accept either "APP:Name|TID:0xXXXXXX" or just raw title
static void parse_app_payload(const char* in, char* outName32) {
    const char* p = strstr(in, "APP:");
    const char* name = p ? p + 4 : in;
    // cut at '|' if present
    char tmp[128];
    size_t i = 0;
    while (name[i] && name[i] != '|' && i < sizeof(tmp) - 1) { tmp[i] = name[i]; i++; }
    tmp[i] = 0;

    // trim whitespace
    while (*tmp == ' ' || *tmp == '\t') ++name, --i;
    // copy up to 31 bytes
    strncpy(outName32, tmp, 31);
    outName32[31] = 0;
}

void Cache_Manager::pollTitleUdp() {
    if (!g_udpBound) {
        // Bind once; failure is non-fatal
        if (g_appUdp.begin(50506)) g_udpBound = true;
        else return;
    }

    char buf[256];
    if (recv_line_udp(g_appUdp, buf, sizeof(buf))) {
        char name[32] = {0};
        parse_app_payload(buf, name);
        if (name[0]) {
            setCurrentApp(name);
        //    Serial.printf("[CacheMgr] Title via UDP: %s\n", name);
        }
    }
}
