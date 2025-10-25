#include "eeprom_min.h"
#include <WiFiUdp.h>
#include <Wire.h>
#include <base64.h>      // ESP32 core
#include <mbedtls/md.h>  // for HMAC-SHA1

#ifndef EEPROM_UDP_PORT
#define EEPROM_UDP_PORT 50506
#endif

// Periodic rebroadcast cadence (can be overridden before including this file)
#ifndef EEPROM_REBROADCAST_MS
#define EEPROM_REBROADCAST_MS 10000UL
#endif

static WiFiUDP eeUdp;

// ---- share the global SMBus lock (defined in xbox_smbus_poll.cpp) ----
extern volatile bool g_smbus_locked;
static inline bool try_lock_smbus() {
  noInterrupts();
  bool ok = !g_smbus_locked;
  if (ok) g_smbus_locked = true;
  interrupts();
  return ok;
}
static inline void unlock_smbus() {
  noInterrupts();
  g_smbus_locked = false;
  interrupts();
}
// Bounded wait for the lock (so we don't give up just because poller was mid-read)
static bool lock_with_timeout(uint32_t ms_timeout = 500) {
  uint32_t t0 = millis();
  do {
    if (try_lock_smbus()) return true;
    delay(2);
  } while (millis() - t0 < ms_timeout);
  return false;
}

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

  // -------- 24C02 read helpers (STOP-only, explicit) ------------
  // NOTE: We acquire the global SMBus lock before doing any reads.
  static inline void i2c_breather() { delayMicroseconds(200); }

  int readBlock(uint8_t eeOffset, uint8_t *out, size_t len) {
    if (!out || len == 0) return -1;

    // Set internal address pointer (use STOP at the end)
    Wire.beginTransmission(XboxEEPROM::I2C_ADDR);
    Wire.write(eeOffset);
    if (Wire.endTransmission(true) != 0)  // STOP (no repeated-start)
      return -1;

    size_t got = 0;
    while (got < len) {
      uint8_t chunk = (uint8_t)((len - got) > 32 ? 32 : (len - got));
      // Explicit STOP at end of each chunk
      uint8_t n = Wire.requestFrom((int)XboxEEPROM::I2C_ADDR, (int)chunk, (int)true);
      if (n == 0) return -1;
      for (uint8_t i = 0; i < n && got < len; ++i) {
        if (!Wire.available()) return -1;
        out[got++] = Wire.read();
      }
      i2c_breather(); // be a good bus citizen between chunks
    }
    return 0;
  }

  int readAll(uint8_t buf[256]) {
    if (!buf) return -1;

    // Entire EEPROM read under the shared SMBus lock
    if (!lock_with_timeout()) return -1;  // couldn't get the bus; fail safely

    int rc = 0;
    for (uint16_t off = 0; off < 256; off += 16) {
      if (readBlock((uint8_t)off, buf + off, 16) != 0) { rc = -1; break; }
    }

    unlock_smbus();
    return rc;
  }

  // --- tiny RC4 ---
  struct rc4_state { uint8_t S[256]; uint8_t i, j; };
  static void rc4_init(rc4_state* st, const uint8_t* key, size_t klen) {
    for (int n = 0; n < 256; ++n) st->S[n] = (uint8_t)n;
    st->i = st->j = 0;
    uint8_t j = 0;
    for (int n = 0; n < 256; ++n) {
      j = (uint8_t)(j + st->S[n] + key[n % klen]);
      uint8_t tmp = st->S[n]; st->S[n] = st->S[j]; st->S[j] = tmp;
    }
  }
  static void rc4_crypt(rc4_state* st, uint8_t* buf, size_t len) {
    uint8_t i = st->i, j = st->j;
    for (size_t n = 0; n < len; ++n) {
      i = (uint8_t)(i + 1);
      j = (uint8_t)(j + st->S[i]);
      uint8_t tmp = st->S[i]; st->S[i] = st->S[j]; st->S[j] = tmp;
      uint8_t K = st->S[(uint8_t)(st->S[i] + st->S[j])];
      buf[n] ^= K;
    }
    st->i = i; st->j = j;
  }

  // --- HMAC-SHA1 via mbedTLS ---
  static bool hmac_sha1(const uint8_t* key, size_t klen,
                        const uint8_t* msg, size_t mlen,
                        uint8_t out20[20]) {
    const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
    if (!info) return false;
    return mbedtls_md_hmac(info, key, klen, msg, mlen, out20) == 0;
  }

  // --- Published EEPROM RC4 keys per revision (16 bytes each) ---
  static const uint8_t EEPROM_KEY_V10   [16] = { 0x2A,0x3B,0xAD,0x2C,0xB1,0x94,0x4F,0x93,0xAA,0xCD,0xCD,0x7E,0x0A,0xC2,0xEE,0x5A };
  static const uint8_t EEPROM_KEY_V11_14[16] = { 0x1D,0xF3,0x5C,0x83,0x8E,0xC9,0xB6,0xFC,0xBD,0xF6,0x61,0xAB,0x4F,0x06,0x33,0xE4 };
  static const uint8_t EEPROM_KEY_V16   [16] = { 0x2B,0x84,0x57,0xBE,0x9B,0x1E,0x65,0xC6,0xCD,0x9D,0x2B,0xCE,0xC1,0xA2,0x09,0x61 };

  // offsets / lengths in EEPROM
  static const int OFF_FACTORY   = 0x14;  // start of encrypted factory section (confounder+HDD+pads)
  static const int LEN_FACTORY   = 0x1C;  // 28 bytes: confounder(8) + HDD key(16) + pad(4)
  // NOTE: the 20-byte stored HMAC-SHA1 for the Factory section is at 0x00..0x13 (not encrypted)
  static const int OFF_HDD_KEY   = 0x1C;  // where HDD key would appear when decrypted
  static const int OFF_CHECKSUM  = 0x00;  // 20-byte stored HMAC-SHA1 of decrypted factory section
  static const int LEN_CHECKSUM  = 0x14;
  static const uint8_t kFactoryLens[2] = { 0x1C, 0x18 };  // try 28 then 24 bytes

  // ---------- one-time ROM + HDD cache for broadcast ----------
  static bool    s_have_rom  = false;
  static uint8_t s_rom[256];
  static bool    s_have_hdd  = false;
  static char    s_hdd_hex[33] = {0};

  // Cached Base64 and timing for periodic rebroadcast
  static String   s_raw_b64;            // Base64 of s_rom (prepared once)
  static uint32_t s_last_bcast = 0;     // last broadcast timestamp
  static bool     s_read_done  = false; // we performed the one-time read (never touch SMBus again)

  // -------- internal helper: broadcast from cached data only ------------
  static void send_broadcasts_from_cache() {
    if (!s_have_rom) return;     // nothing to send yet
    ensureUdp();

    // Prepare cached Base64 once
    if (s_raw_b64.length() == 0) {
      s_raw_b64 = base64::encode(s_rom, 256);
    }

    const uint8_t* rom = s_rom;

    // Optional debug prints trimmed down
    for (int i=0;i<12;i++) { Serial.printf("%02X ", rom[0x14+i]); } Serial.println();
    for (int i=0;i<12;i++) { Serial.printf("%02X ", rom[0x09+i]); } Serial.println();

    // RAW packet
    eeUdp.beginPacket(IPAddress(255,255,255,255), EEPROM_UDP_PORT);
    eeUdp.print("EE:RAW=");
    eeUdp.print(s_raw_b64);
    eeUdp.endPacket();

    // HDD packet
    if (s_have_hdd) {
      eeUdp.beginPacket(IPAddress(255,255,255,255), EEPROM_UDP_PORT);
      eeUdp.print("EE:HDD=");
      eeUdp.print(s_hdd_hex);
      eeUdp.endPacket();
      Serial.println("[EE] HDD packet broadcast (EE:HDD=...)");
    }

    // Duplicate RAW (preserved behavior)
    eeUdp.beginPacket(IPAddress(255,255,255,255), EEPROM_UDP_PORT);
    eeUdp.print("EE:RAW=");
    eeUdp.print(s_raw_b64);
    eeUdp.endPacket();

    // Labeled packet (optional)
    if (s_have_hdd) {
      char snP[13];  cleanSerial(&rom[0x34], 12, snP);
      char macP[18]; macToStr(&rom[0x40], macP);
      const char* regP = regionName(rom[0x58]);

      eeUdp.beginPacket(IPAddress(255,255,255,255), EEPROM_UDP_PORT);
      eeUdp.print("EE:SN=");  eeUdp.print(snP);
      eeUdp.print("|MAC=");   eeUdp.print(macP);
      eeUdp.print("|REG=");   eeUdp.print(regP);
      eeUdp.print("|HDD=");   eeUdp.print(s_hdd_hex);
      eeUdp.print("|RAW=");   eeUdp.print(s_raw_b64);
      eeUdp.endPacket();
    }
  }

  // -------- one-shot read + immediate broadcast (subsequent calls just rebroadcast) ------------
  void broadcastOnce() {
    ensureUdp();

    // One-time SMBus read & decrypt/cache
    if (!s_read_done) {
      if (!s_have_rom) {
        if (readAll(s_rom) != 0) {
          Serial.println("[EE] readAll FAILED");
          eeUdp.beginPacket(IPAddress(255,255,255,255), EEPROM_UDP_PORT);
          eeUdp.print("EE:ERR=READ_FAIL");
          eeUdp.endPacket();
          s_read_done = true; // prevent retrying I2C forever
          return;
        }

        // Decrypt HDD key once and cache result (CPU-only; no SMBus)
        const uint8_t* chk = &s_rom[OFF_CHECKSUM];
        const uint8_t* candidates[3] = { EEPROM_KEY_V10, EEPROM_KEY_V11_14, EEPROM_KEY_V16 };
        // const char*    cand_name [3] = { "v1.0", "v1.1-1.4", "v1.6/1.6b" };

        for (int k = 0; k < 3 && !s_have_hdd; ++k) {
          uint8_t rc4key[20];
          if (!hmac_sha1(candidates[k], 16, chk, LEN_CHECKSUM, rc4key)) continue;

          uint8_t fac[LEN_FACTORY];
          memcpy(fac, &s_rom[OFF_FACTORY], LEN_FACTORY);
          rc4_state st; rc4_init(&st, rc4key, sizeof(rc4key));

          // try both lengths
          for (int li = 0; li < 2 && !s_have_hdd; ++li) {
            const int fac_len = kFactoryLens[li];
            uint8_t tmp[LEN_FACTORY];
            memcpy(tmp, fac, LEN_FACTORY);
            rc4_state st2 = st;           // copy state for fresh decrypt per length
            rc4_crypt(&st2, tmp, fac_len);

            uint8_t tmpHmac[20];
            if (!hmac_sha1(candidates[k], 16, tmp, fac_len, tmpHmac)) continue;
            if (memcmp(tmpHmac, chk, LEN_CHECKSUM) != 0) continue;

            toHexUpper(&tmp[8], 16, s_hdd_hex);
            s_have_hdd = true;
          }
        }

        s_have_rom = true; // mark snapshot complete
      }

      // Prepare cached Base64 once (optional; helper also ensures it)
      if (s_raw_b64.length() == 0) {
        s_raw_b64 = base64::encode(s_rom, 256);
      }

      s_read_done = true;  // never touch SMBus again
    }

    // Immediate broadcast using cached data
    send_broadcasts_from_cache();
    s_last_bcast = millis();
  }

  // -------- periodic rebroadcast (call from loop()) ------------
  void tick() {
    if (!s_have_rom) return;  // nothing cached yet (read failed or not run)
    const uint32_t now = millis();
    if (now - s_last_bcast >= EEPROM_REBROADCAST_MS) {
      send_broadcasts_from_cache();
      s_last_bcast = now;
    }
  }

}  // namespace XboxEEPROM
