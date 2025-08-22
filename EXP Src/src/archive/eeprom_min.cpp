#include "eeprom_min.h"
#include <WiFiUdp.h>
#include <Wire.h>
#include <base64.h>  // ESP32 core

#ifndef EEPROM_UDP_PORT
#define EEPROM_UDP_PORT 50506
#endif

static WiFiUDP eeUdp;

namespace XboxEEPROM {

  static bool begun = false;

  static void ensureUdp() {
    if (!begun) {
      eeUdp.begin(EEPROM_UDP_PORT);
      begun = true;
    }
  }

  // -------- helpers (local only) ----------
  static inline char nyb_to_hex(uint8_t v) {
    v &= 0x0F;
    return (v < 10) ? char('0' + v) : char('A' + (v - 10));
  }
  static void toHexUpper(const uint8_t* src, size_t n, char* out /*2n+1*/) {
    for (size_t i = 0; i < n; ++i) {
      out[2*i + 0] = nyb_to_hex(src[i] >> 4);
      out[2*i + 1] = nyb_to_hex(src[i]);
    }
    out[2*n] = '\0';
  }
  static void macToStr(const uint8_t mac[6], char* out /*"XX:XX:XX:XX:XX:XX"*/) {
    char* p = out;
    for (int i = 0; i < 6; ++i) {
      *p++ = nyb_to_hex(mac[i] >> 4);
      *p++ = nyb_to_hex(mac[i]);
      if (i != 5) *p++ = ':';
    }
    *p = '\0';
  }
  static const char* regionName(uint8_t r) {
    switch (r & 0xFF) {
      case 0x00: return "NTSC-U";
      case 0x01: return "NTSC-J";
      case 0x02: return "PAL";
      default:   return "UNKNOWN";
    }
  }
  // Clean ASCII (stop on NUL/0xFF, keep A–Z/0–9, upper-case)
  static void cleanSerial(const uint8_t* src, size_t n, char* out /*n+1*/) {
    size_t j = 0;
    for (size_t i = 0; i < n; ++i) {
      uint8_t b = src[i];
      if (b == 0x00 || b == 0xFF) break;
      char c = (char)b;
      if (c >= 'a' && c <= 'z') c = char(c - 32);
      if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
        out[j++] = c;
      }
    }
    out[j] = '\0';
  }

  // -------- 24C02 read helpers ------------
  int readBlock(uint8_t eeOffset, uint8_t *out, size_t len) {
    if (!out || len == 0) return -1;

    Wire.beginTransmission(I2C_ADDR);
    Wire.write(eeOffset);                    // set internal address pointer
    if (Wire.endTransmission(false) != 0)    // repeated start
      return -1;

    size_t got = 0;
    while (got < len) {
      uint8_t chunk = (uint8_t)((len - got) > 32 ? 32 : (len - got));
      uint8_t n = Wire.requestFrom((int)I2C_ADDR, (int)chunk);
      if (n == 0) return -1;
      for (uint8_t i = 0; i < n && got < len; ++i) {
        if (!Wire.available()) return -1;
        out[got++] = Wire.read();
      }
    }
    return 0;
  }

  int readAll(uint8_t buf[256]) {
    if (!buf) return -1;
    for (uint16_t off = 0; off < 256; off += 16) {
      if (readBlock((uint8_t)off, buf + off, 16) != 0) return -1;
    }
    return 0;
  }

  // -------- one-shot broadcast ------------
  void broadcastOnce() {
    ensureUdp();

    uint8_t rom[256];
    if (readAll(rom) != 0) {
      // Debug + small error packet so PC side shows "EEPROM error"
      Serial.println("[EE] readAll FAILED");
      eeUdp.beginPacket(IPAddress(255,255,255,255), EEPROM_UDP_PORT);
      eeUdp.print("EE:ERR=READ_FAIL");
      eeUdp.endPacket();
      return;
    }

    // ---- Debug: try BOTH known layouts and print them ----
    // Layout A (firmware newer): SN@0x14, MAC@0x24, HDD@0x50
    char snA[13];  cleanSerial(&rom[0x14], 12, snA);
    char macA[18]; macToStr(&rom[0x24], macA);
    char hddA[33]; toHexUpper(&rom[0x50], 16, hddA);

    // Layout B (retail-common): SN@0x09, MAC@0x3C, HDD@0x04
    char snB[13];  cleanSerial(&rom[0x09], 12, snB);
    char macB[18]; macToStr(&rom[0x3C], macB);
    char hddB[33]; toHexUpper(&rom[0x04], 16, hddB);

    const char* reg = regionName(rom[0x58]);

    Serial.println("[EE] --- decoded candidates ---");
    Serial.printf("[EE] A: SN='%s'  MAC=%s  HDD=%s  REG=%s\n", snA, macA, hddA, reg);
    Serial.printf("[EE] B: SN='%s'  MAC=%s  HDD=%s  REG=%s\n", snB, macB, hddB, reg);

    // Also print a short hexdump of the serial regions for sanity
    Serial.print("[EE] raw @0x14: ");
    for (int i=0;i<12;i++) { Serial.printf("%02X ", rom[0x14+i]); } Serial.println();
    Serial.print("[EE] raw @0x09: ");
    for (int i=0;i<12;i++) { Serial.printf("%02X ", rom[0x09+i]); } Serial.println();

    // ---- Network: keep it simple for PC viewer — send RAW only ----
    // The PC script will auto-parse RAW and choose the right layout.
    String b64 = base64::encode(rom, sizeof(rom));
    eeUdp.beginPacket(IPAddress(255,255,255,255), EEPROM_UDP_PORT);
    eeUdp.print("EE:RAW=");
    eeUdp.print(b64);
    eeUdp.endPacket();

    Serial.println("[EE] RAW packet broadcast (EE:RAW=...)");
  }

  // Non-reading placeholder (no SMBus traffic)
  void rebroadcast() {
    // Intentionally empty: avoid hammering the SMBus.
    // (PC side already latched the one-shot RAW.)
  }
}  // namespace XboxEEPROM
