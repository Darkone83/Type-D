// sleepmgr.cpp
#include "sleepmgr.h"
#include <Preferences.h>

namespace {
    Preferences prefs;
    bool enabled = true;
    uint8_t timeoutIdx = 0;
    unsigned long lastInteraction = 0;
    bool sleeping = false;
}

namespace SleepMgr {

void begin() {
    prefs.begin("sleepmgr", false);
    enabled = prefs.getBool("enabled", true);
    timeoutIdx = prefs.getUChar("timeout", 0);
    lastInteraction = millis();
    sleeping = false;
}

void save() {
    prefs.putBool("enabled", enabled);
    prefs.putUChar("timeout", timeoutIdx);
}

void resetTimer() {
    lastInteraction = millis();
    if (sleeping) {
        wake();
    }
}

void setEnabled(bool en) {
    enabled = en;
    save();
    if (!enabled && sleeping) {
        wake();
    }
}

bool isEnabled() { return enabled; }

void setTimeoutIndex(uint8_t idx) {
    if (idx < numTimeouts) {
        timeoutIdx = idx;
        save();
    }
}
uint8_t getTimeoutIndex() { return timeoutIdx; }
uint16_t getTimeoutSeconds() { return timeouts[timeoutIdx] * 60; }

bool isSleeping() { return sleeping; }

void update() {
    if (!enabled) {
        sleeping = false;
        return;
    }
    if (!sleeping && (millis() - lastInteraction > getTimeoutSeconds() * 1000UL)) {
        sleeping = true;
        // Add any screen-off logic here (UI integration)
    }
}

void wake() {
    sleeping = false;
    lastInteraction = millis();
    // Add any screen-on logic here (UI integration)
}

} // namespace SleepMgr
