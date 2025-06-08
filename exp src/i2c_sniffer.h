// i2c_sniffer.h
#pragma once

#include <stdint.h>

struct I2C_Event {
    enum Type {
        START,
        STOP,
        ADDRESS, // 8-bit (includes R/W)
        DATA,    // 8-bit
        ACK,
        NACK
    } type;
    uint8_t value;   // For ADDRESS/DATA, the byte value
    bool isRead;     // For ADDRESS, true if read operation (R/W bit set)
};

namespace I2C_Sniffer {
    void begin(uint8_t sdaPin, uint8_t sclPin);
    bool getNextEvent(I2C_Event &evt);
}
