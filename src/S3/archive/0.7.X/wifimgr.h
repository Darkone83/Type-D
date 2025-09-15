#pragma once

#include <Arduino.h>

namespace WiFiMgr {
    void begin();
    void loop();
    void restartPortal();
    void forgetWiFi();
    bool isConnected();
    String getStatus();
}
