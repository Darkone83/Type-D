#pragma once
#include <stdint.h>

struct XboxStatus {
    int cpuTemp = -1000;
    int ambientTemp = -1000;
    int fanSpeed = -1;
    char currentApp[16] = {0};
    unsigned long cpuTempUpdated = 0;
    unsigned long ambientTempUpdated = 0;
    unsigned long fanSpeedUpdated = 0;
    unsigned long appUpdated = 0;
};

namespace Cache_Manager {
    void begin();
    void updateCpuTemp(int tempC);
    void updateAmbientTemp(int tempC);
    void updateFan(int fanPct);
    void updateApp(const char* app);
    const XboxStatus& getStatus();
    void clear();
}
