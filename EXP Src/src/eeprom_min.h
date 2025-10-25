#pragma once
#include <Arduino.h>

// Periodic rebroadcast cadence (can be overridden before including this header)
#ifndef EEPROM_REBROADCAST_MS
#define EEPROM_REBROADCAST_MS 10000UL
#endif

namespace XboxEEPROM {
  // 7-bit I2C address for Xbox 24C02 EEPROM (A2/A1/A0 strapped -> 0x54).
  static const uint8_t I2C_ADDR = 0x54;

  // Read `len` bytes starting at `eeOffset` into `out`. Returns 0 on success.
  int readBlock(uint8_t eeOffset, uint8_t *out, size_t len);

  // Read full 256-byte EEPROM into `buf[256]`. Returns 0 on success.
  int readAll(uint8_t buf[256]);

  // Send one-shot UDP broadcast (uses cached data after first read).
  void broadcastOnce();

  // Periodic rebroadcast from cached EEPROM/HDD data; call regularly (e.g., from loop()).
  void tick();
}
