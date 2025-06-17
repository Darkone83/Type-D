#pragma once

#include <stdint.h>

// Forward declare your event/structs
struct I2C_Event;
struct XboxStatus;

namespace Debug_Logger {
    void logI2CEvent(const I2C_Event &evt);
    void logStatusTransmission(const XboxStatus &status);
}
