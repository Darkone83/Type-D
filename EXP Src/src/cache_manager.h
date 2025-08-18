// cache_manager.h
#pragma once
#include <Arduino.h>

struct XboxStatus {
    int fanSpeed = -1;
    int cpuTemp = -1000;
    int ambientTemp = -1000;
    char currentApp[32] = {0};
};

struct XboxSMBusStatus; // fwd

namespace Cache_Manager {
    void begin();
    void setFanSpeed(int percent);
    void setCpuTemp(int celsius);
    void setAmbientTemp(int celsius);
    void setCurrentApp(const char *name);

    // NOTE: remove 'static' here so we can link the definition in the .cpp
    void pollTitleUdp();

    const XboxStatus& getStatus();
    void reset();
    void updateFromSmbus(const XboxSMBusStatus& st);
}
