#pragma once

#include <Arduino.h>

// Main API for WiFiMgr
namespace WiFiMgr {
    void begin();
    void loop();
    void restartPortal();
    void forgetWiFi();
    bool isConnected();
    String getStatus();
}
