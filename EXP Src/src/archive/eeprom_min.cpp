#include "eeprom_min.h"
#include <WiFiUdp.h>
#include <Wire.h>
#include <base64.h>  // ESP32 core
#include <mbedtls/md.h>  // for HMAC-SHA1

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
static const int OFF_HDD_KEY   = 0x1C;  // overall EEPROM offset where HDD key would appear when decrypted
static const int OFF_CHECKSUM  = 0x00;  // 20-byte stored HMAC-SHA1 of decrypted factory section
static const int LEN_CHECKSUM  = 0x14;
static const uint8_t kFactoryLens[2] = { 0x1C, 0x18 };  // try 28 then 24 bytes

  // -------- one-shot broadcast ------------
void broadcastOnce() {
  ensureUdp();

  uint8_t rom[256];
  if (readAll(rom) != 0) {
    Serial.println("[EE] readAll FAILED");
    eeUdp.beginPacket(IPAddress(255,255,255,255), EEPROM_UDP_PORT);
    eeUdp.print("EE:ERR=READ_FAIL");
    eeUdp.endPacket();
    return;
  }

  // ==== HDD key decrypt (try all EEPROM keys, and both factory lengths 0x1C/0x18) ====
  char hdd_plain_hex[33] = {0};
  bool have_hdd = false;

  const uint8_t* candidates[3] = { EEPROM_KEY_V10, EEPROM_KEY_V11_14, EEPROM_KEY_V16 };
  const char*    cand_name [3] = { "v1.0", "v1.1-1.4", "v1.6/1.6b" };

  // Stored HMAC-SHA1 of the factory section is at OFF_CHECKSUM..+0x13 (not encrypted)
  const uint8_t* chk = &rom[OFF_CHECKSUM];

  // Some EEPROMs validate 0x1C bytes (confounder+key+pad), others only 0x18 (no pad)
  static const uint8_t kFactoryLens[2] = { 0x1C, 0x18 };

  for (int k = 0; k < 3 && !have_hdd; ++k) {
    // RC4 key = HMAC-SHA1( EEPROM_KEY_variant, stored Checksum[20] )
    uint8_t rc4key[20];
    if (!hmac_sha1(candidates[k], 16, chk, LEN_CHECKSUM, rc4key)) continue;

    for (int li = 0; li < 2 && !have_hdd; ++li) {
      const int fac_len = kFactoryLens[li];

      // decrypt a local copy of the factory section (start at OFF_FACTORY)
      uint8_t fac[LEN_FACTORY];                   // LEN_FACTORY is 0x1C (max needed)
      memcpy(fac, &rom[OFF_FACTORY], sizeof(fac));
      rc4_state st; rc4_init(&st, rc4key, sizeof(rc4key));
      rc4_crypt(&st, fac, fac_len);

      // validate: HMAC-SHA1( EEPROM_KEY_variant, fac[0..fac_len) ) == stored checksum
      uint8_t tmpHmac[20];
      if (!hmac_sha1(candidates[k], 16, fac, fac_len, tmpHmac)) continue;
      if (memcmp(tmpHmac, chk, LEN_CHECKSUM) != 0) continue;

      // success → HDD key is 16 bytes right after the 8-byte confounder
      toHexUpper(&fac[8], 16, hdd_plain_hex);
      have_hdd = true;
      Serial.printf("[EE] HDD key decrypted OK using %s key (len=0x%02X): %s\n",
                    cand_name[k], fac_len, hdd_plain_hex);
    }
  }

  if (!have_hdd) {
    Serial.println("[EE] HDD key decrypt/validate FAILED with all variants/lengths");
  }

  // ---- Debug: try BOTH known layouts and print them ----
  char snA[13];  cleanSerial(&rom[0x14], 12, snA);
  char macA[18]; macToStr(&rom[0x24], macA);
  char hddA[33]; toHexUpper(&rom[0x50], 16, hddA);

  char snB[13];  cleanSerial(&rom[0x09], 12, snB);
  char macB[18]; macToStr(&rom[0x3C], macB);
  char hddB[33]; toHexUpper(&rom[0x04], 16, hddB);

  const char* reg = regionName(rom[0x58]);

  Serial.println("[EE] --- decoded candidates ---");
  Serial.printf("[EE] A: SN='%s'  MAC=%s  HDD=%s  REG=%s\n", snA, macA, hddA, reg);
  Serial.printf("[EE] B: SN='%s'  MAC=%s  HDD=%s  REG=%s\n", snB, macB, hddB, reg);

  Serial.print("[EE] raw @0x14: ");
  for (int i=0;i<12;i++) { Serial.printf("%02X ", rom[0x14+i]); } Serial.println();
  Serial.print("[EE] raw @0x09: ");
  for (int i=0;i<12;i++) { Serial.printf("%02X ", rom[0x09+i]); } Serial.println();

  // ---- Keep existing RAW packet unchanged ----
  String b64 = base64::encode(rom, sizeof(rom));
  eeUdp.beginPacket(IPAddress(255,255,255,255), EEPROM_UDP_PORT);
  eeUdp.print("EE:RAW=");
  eeUdp.print(b64);
  eeUdp.endPacket();
  Serial.println("[EE] RAW packet broadcast (EE:RAW=...)");

  // ---- Also broadcast plaintext HDD key if recovered (unchanged behavior otherwise) ----
  if (have_hdd) {
    eeUdp.beginPacket(IPAddress(255,255,255,255), EEPROM_UDP_PORT);
    eeUdp.print("EE:HDD=");
    eeUdp.print(hdd_plain_hex);
    eeUdp.endPacket();
    Serial.println("[EE] HDD packet broadcast (EE:HDD=...)");
  }

// ---- Network: keep existing RAW packet unchanged ----
//String b64 = base64::encode(rom, sizeof(rom));
eeUdp.beginPacket(IPAddress(255,255,255,255), EEPROM_UDP_PORT);
eeUdp.print("EE:RAW=");
eeUdp.print(b64);
eeUdp.endPacket();
Serial.println("[EE] RAW packet broadcast (EE:RAW=...)");

// ---- NEW: send labeled packet so PC app uses our decrypted HDD (and won't overwrite from RAW) ----
if (have_hdd) {
  // Use the PC viewer’s primary map for labeled fields:
  char snP[13];  cleanSerial(&rom[0x34], 12, snP);     // SERIAL @0x34 (12)
  char macP[18]; macToStr(&rom[0x40], macP);           // MAC    @0x40 (6)
  const char* regP = regionName(rom[0x58]);            // REGION @0x58 (1)

  eeUdp.beginPacket(IPAddress(255,255,255,255), EEPROM_UDP_PORT);
  eeUdp.print("EE:SN=");  eeUdp.print(snP);
  eeUdp.print("|MAC=");   eeUdp.print(macP);
  eeUdp.print("|REG=");   eeUdp.print(regP);
  eeUdp.print("|HDD=");   eeUdp.print(hdd_plain_hex);  // <-- decrypted HDD key
  eeUdp.print("|RAW=");   eeUdp.print(b64);            // reuse existing variable; DON'T redeclare
  eeUdp.endPacket();

  Serial.printf("[EE] labeled packet broadcast (SN|MAC|REG|HDD|RAW) with HDD=%s\n", hdd_plain_hex);
}
}


}  // namespace XboxEEPROM
