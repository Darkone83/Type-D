#pragma once

#include <Arduino.h>

// Structure to hold polled values (adapt/expand as needed)
struct XboxSMBusStatus {
    int cpuTemp = -1;
    int boardTemp = -1;
    int fanSpeed = -1;
    char app[16] = {0};
    // Add more fields as needed
};

namespace XboxSMBusPoll {
    void begin(uint8_t sdaPin = 7, uint8_t sclPin = 6);
    bool poll(XboxSMBusStatus& status); // Returns true if poll successful
}
