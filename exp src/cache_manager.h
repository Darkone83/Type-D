// cache_manager.h
#pragma once

struct XboxStatus {
    int fanSpeed = -1;
    int cpuTemp = -1000;
    int ambientTemp = -1000;
    char currentApp[32] = "";
    char ipAddress[16] = "";
    char macAddress[18] = "";
    // Expand as needed for other fields!
};

namespace Cache_Manager {
    void begin();
    void updateFan(int speed);
    void updateCpuTemp(int temp);
    void updateAmbientTemp(int temp);
    void updateApp(const char *name);
    void updateIp(const char *ip);
    void updateMac(const char *mac);
    const XboxStatus& getStatus();
}
