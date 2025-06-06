// File: i2c_sniffer.h

#ifndef I2C_SNIFFER_H
#define I2C_SNIFFER_H

#include <Arduino.h>

// Data structure to describe I2C event (address, data, direction, etc)
struct I2C_Event {
  enum Type { START, STOP, ADDRESS, DATA, ACK, NACK } type;
  uint8_t value;
  uint8_t address;
  bool isRead;
};

namespace I2C_Sniffer {
  void begin(uint8_t sdaPin, uint8_t sclPin);
  bool getNextEvent(I2C_Event &evt);
}

#endif
