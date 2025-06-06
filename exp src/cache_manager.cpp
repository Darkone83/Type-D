// cache_manager.cpp
#include "cache_manager.h"
#include <Arduino.h>
#include <string.h>

static XboxStatus status;

void Cache_Manager::begin() {
    memset(&status, 0, sizeof(status));
    status.fanSpeed = -1;
    status.cpuTemp = -1000;
    status.ambientTemp = -1000;
    status.currentApp[0] = '\0';
    status.ipAddress[0] = '\0';
    status.macAddress[0] = '\0';
}

void Cache_Manager::updateFan(int speed) {
    status.fanSpeed = speed;
}

void Cache_Manager::updateCpuTemp(int temp) {
    status.cpuTemp = temp;
}

void Cache_Manager::updateAmbientTemp(int temp) {
    status.ambientTemp = temp;
}

void Cache_Manager::updateApp(const char *name) {
    strncpy(status.currentApp, name, sizeof(status.currentApp) - 1);
    status.currentApp[sizeof(status.currentApp) - 1] = '\0';
}

void Cache_Manager::updateIp(const char *ip) {
    strncpy(status.ipAddress, ip, sizeof(status.ipAddress) - 1);
    status.ipAddress[sizeof(status.ipAddress) - 1] = '\0';
}

void Cache_Manager::updateMac(const char *mac) {
    strncpy(status.macAddress, mac, sizeof(status.macAddress) - 1);
    status.macAddress[sizeof(status.macAddress) - 1] = '\0';
}

const XboxStatus& Cache_Manager::getStatus() {
    return status;
}
