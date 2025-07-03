#pragma once
#include <Arduino.h>

namespace SMBusExt {
    void begin();
    void loop();

    // Extended status structure (no display/0x3C, just trayState, avPackState, picVer)
    struct Status {
        int trayState;
        int avPackState;
        int picVer;
    };

    // Send the current extended status via UDP
    void sendExtStatus();   // <-- NO ARGUMENTS

    // Optionally: send any custom status struct
    void sendCustomStatus(const Status& status);
}
