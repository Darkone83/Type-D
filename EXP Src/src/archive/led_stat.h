#pragma once

enum class LedStatus {
    Booting,
    Portal,
    WifiConnected,
    WifiFailed,
    UdpTransmit
};

namespace LedStat {
    void begin();
    void setStatus(LedStatus status);
    void loop(); // Call this in main loop for blinking/timing
}
