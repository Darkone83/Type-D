#pragma once
#include <Arduino.h>

namespace XboxEEPROM {
  // 7-bit I2C address for Xbox 24C02 EEPROM (A2/A1/A0 strapped -> 0x54).
  static const uint8_t I2C_ADDR = 0x54;

  // Read `len` bytes starting at `eeOffset` into `out`. Returns 0 on success.
  int readBlock(uint8_t eeOffset, uint8_t *out, size_t len);

  // Read full 256-byte EEPROM into `buf[256]`. Returns 0 on success.
  int readAll(uint8_t buf[256]);

  // Send one-line UDP broadcast: EE:RAW=<base64 of 256 bytes>
  void broadcastOnce();
}
