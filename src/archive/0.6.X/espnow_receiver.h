#pragma once
#include <esp_now.h>
#include <WiFi.h>

struct XboxPacket {
    int fanSpeed;
    int cpuTemp;
    int ambientTemp;
    char app[16];
};

class ESPNOWReceiver {
public:
    static void begin();
    static bool hasPacket();
    static XboxPacket getLatest();
};
