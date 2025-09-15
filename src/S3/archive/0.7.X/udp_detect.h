#pragma once
#include <Arduino.h>
#include "xbox_status.h"

namespace UDPDetect {
    void begin();
    void loop();
    bool hasPacket();
    const XboxStatus& getLatest();
    void acknowledge();
}
