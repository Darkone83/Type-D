// sleepmgr.h
#pragma once

#include <Arduino.h>

namespace SleepMgr {
    // Available timeouts in minutes
    static const uint8_t timeouts[] = {1, 2, 5, 10};
    static const uint8_t numTimeouts = sizeof(timeouts) / sizeof(timeouts[0]);

    void begin();
    void resetTimer();
    void update(); // Call periodically to check if sleep should activate

    void setEnabled(bool enabled);
    bool isEnabled();

    void setTimeoutIndex(uint8_t idx);
    uint8_t getTimeoutIndex();
    uint16_t getTimeoutSeconds();

    bool isSleeping();
    void wake();

    void save(); // Save current state to NVS/Preferences
}
