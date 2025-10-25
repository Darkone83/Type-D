// smbus_ext.cpp
//
// Extended, read-only SMBus sampler for the Original Xbox.
// Safe across all revisions, with special care for 1.6 (Xcalibur).
//
// Key points:
// - Uses the cross-core SMBus mutex provided by xbox_smbus_poll.cpp
//   (try_lock_smbus()/unlock_smbus()), so only ONE reader touches the bus.
// - STOP-only transactions (no repeated-start) for maximum 1.6 compatibility.
// - Gentle pacing with jitter and backoff; small inter-op gaps.
// - Encoder is detected once; Xcalibur video mode is probed SAFELY and
//   periodically (single register, STOP-only) so mode changes are tracked.
// - If base SMC reads fail, we back off and skip transmit to reduce pressure.
// - No writes are performed to SMBus devices.
//
// Packet format remains via SMBusExt::Status.
//

#include "smbus_ext.h"
#include <Arduino.h>
#include <WiFiUdp.h>
#include <Wire.h>

// We only need the wrappers; they’re defined in xbox_smbus_poll.cpp
extern bool     try_lock_smbus();
extern void     unlock_smbus();
extern uint32_t smbus_last_activity_ms(); // (optional pacing hint)

// ===================== Config =====================
#define SMBUS_EXT_PORT 50505

// SMBus / I2C addresses
#define SMC_ADDRESS    0x10

// Video encoders (device addresses)
#define ENC_CONEXANT 0x45
#define ENC_FOCUS    0x6A
#define ENC_XCALIBUR 0x70

// SMC registers
#define SMC_TRAY       0x03
#define SMC_AVSTATE    0x04
#define SMC_VER        0x01
#define SMC_CONSOLEVER 0x00   // may be 0xFF (not reported on some boards)

// Small idle gap to be nice to the bus timing
#ifndef SMBUS_GAP_US
#define SMBUS_GAP_US 300
#endif

// Pacing (slower than before to further reduce contention)
#ifndef SMBUS_EXT_STARTUP_GRACE_MS
#define SMBUS_EXT_STARTUP_GRACE_MS 10000
#endif
#ifndef SMBUS_EXT_MIN_PERIOD_MS
#define SMBUS_EXT_MIN_PERIOD_MS     4000   // steady state cadence (~4s)
#endif
#ifndef SMBUS_EXT_BACKOFF_MS
#define SMBUS_EXT_BACKOFF_MS        9000   // slower after errors
#endif

// Probe Xcalibur mode safely (single reg) and periodically so changes show up
#ifndef XCAL_MODE_PROBE_PERIOD_MS
#define XCAL_MODE_PROBE_PERIOD_MS   12000  // ~12s between probes
#endif

// Optional: keep debug prints light
#ifndef SMBUS_EXT_DEBUG
#define SMBUS_EXT_DEBUG 0
#endif

// ===================== UDP ========================
static WiFiUDP extUdp;

// pacing state
static uint32_t g_ext_first_ms = 0;
static uint32_t g_ext_next_allowed_ms = 0;

// one-shot encoder detect cache
static bool s_encoder_known = false;
static int  s_encoder_cache = -1;

// Xcalibur mode cache + pacing (re-probe to catch runtime changes)
static bool     s_xcal_mode_known    = false;
static int      s_xcal_mode_code     = -1; // 0..5 if known
static uint32_t s_xcal_next_probe_ms = 0;

// ===================== SMBus helpers ==============

static inline void smbus_breather() {
  delayMicroseconds(SMBUS_GAP_US);
  // yield();  // (optional) keep disabled to minimize jitter on tight loops
}

// STOP-only single byte read (1.6-safe)
static int readByteSTOP(uint8_t address, uint8_t reg, uint8_t& value) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  if (Wire.endTransmission(true) != 0) return -1;     // STOP
  smbus_breather();
  const uint8_t n = Wire.requestFrom((int)address, 1, (int)true); // STOP
  if (n == 1 && Wire.available()) {
    value = Wire.read();
    smbus_breather();
    return 0;
  }
  return -1;
}

// STOP-only 16-bit read (msb:lsb)
static int readWordSTOP(uint8_t address, uint8_t reg, uint16_t& value) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  if (Wire.endTransmission(true) != 0) return -1;     // STOP
  smbus_breather();
  const uint8_t n = Wire.requestFrom((int)address, 2, (int)true); // STOP
  if (n == 2 && Wire.available() >= 2) {
    const uint8_t msb = Wire.read();
    const uint8_t lsb = Wire.read();
    value = ((uint16_t)msb << 8) | lsb;
    smbus_breather();
    return 0;
  }
  return -1;
}

// ===================== AV-pack heuristics =========
static bool isPalFromAvPack(int avVal) {
  const int v = avVal & 0xFF;
  if (v == 0x00) return true;          // SCART (primary table)
  if ((v & 0x0E) == 0x0E) return true; // SCART (fallback even-nibble)
  return false;
}

// ===================== Conexant resolution ========
static void getConexantResolutionFromRegs(int avVal, int& width, int& height) {
  width = -1; height = -1;

  // Conexant: HDTV_EN (bit7) and RASTER_SEL (bits1..0) @ 0x2E
  uint8_t r2e = 0;
  if (readByteSTOP(ENC_CONEXANT, 0x2E, r2e) == 0) {
    const bool hdtv = (r2e & 0x80) != 0;
    const uint8_t ras = (r2e & 0x03);
    if (hdtv) {
      switch (ras) {
        case 0x01: width = 720;   height = 480;  break; // 480p
        case 0x02: width = 1280;  height = 720;  break; // 720p
        case 0x03: width = 1920;  height = 1080; break; // 1080i
        default: break; // 00 = external timing
      }
    }
  }
  if (width <= 0 || height <= 0) {
    // SD fallback from AV pack
    const bool pal = isPalFromAvPack(avVal);
    width  = 720;
    height = pal ? 576 : 480;
  }
}

// ===================== Focus resolution ===========
static void getFocusResolutionOrFallback(int avVal, int& width, int& height) {
  width = -1; height = -1;

  uint16_t hact = 0, vact = 0;
  if (readWordSTOP(ENC_FOCUS, 0xBA, hact) == 0) width  = (int)(hact & 0x0FFF);
  if (readWordSTOP(ENC_FOCUS, 0xBE, vact) == 0) height = (int)(vact & 0x0FFF);
  if (width  <= 0) { uint16_t np = 0; if (readWordSTOP(ENC_FOCUS, 0x71, np) == 0) width  = (int)(np & 0x07FF); }
  if (height <= 0) { uint16_t nl = 0; if (readWordSTOP(ENC_FOCUS, 0x57, nl) == 0) height = (int)(nl & 0x07FF); }

  if (width <= 0 || height <= 0) {
    const bool pal = isPalFromAvPack(avVal);
    width = 720; height = pal ? 576 : 480;
  }
}

// ===================== Xcalibur helpers ===========

static inline void xcalFallbackFromAv(int avVal, int& w, int& h) {
  const bool pal = isPalFromAvPack(avVal);
  w = 720; h = pal ? 576 : 480;
}

// Map Xcalibur mode code (0..5) to width/height.
// 0:480i, 1:480p, 2:576i, 3:576p, 4:720p, 5:1080i
static void xcalCodeToWH(uint8_t code, int avVal, int& w, int& h) {
  switch (code & 0x07) {
    case 0: w = 720;  h = 480;  break;
    case 1: w = 720;  h = 480;  break;
    case 2: w = 720;  h = 576;  break;
    case 3: w = 720;  h = 576;  break;
    case 4: w = 1280; h = 720;  break;
    case 5: w = 1920; h = 1080; break;
    default: xcalFallbackFromAv(avVal, w, h); break;
  }
}

// Probe Xcal mode once per period (single, safe STOP-only read).
static void maybeProbeXcalMode(uint32_t now_ms) {
  if (now_ms < s_xcal_next_probe_ms) return; // not time yet
  s_xcal_next_probe_ms = now_ms + XCAL_MODE_PROBE_PERIOD_MS;

  // Only meaningful if we know encoder is Xcalibur.
  if (s_encoder_cache != ENC_XCALIBUR) return;

  uint8_t val = 0;
  // Use a single known “mode” register (heuristic): 0x1C.
  if (readByteSTOP(ENC_XCALIBUR, 0x1C, val) == 0) {
    const uint8_t code = (uint8_t)(val & 0x07);
    s_xcal_mode_code  = (code <= 5) ? (int)code : -1;
    s_xcal_mode_known = true;
  } else {
    // leave previous value; mark as known only after a success
    // (so we keep trying next period)
  }
}

// ===================== Encoder detect =============
// Detect ONCE; encoders don’t change at runtime.
static void detectEncoderOnce() {
  if (s_encoder_known) return;
  uint8_t dummy;
  if (readByteSTOP(ENC_CONEXANT, 0x00, dummy) == 0) {
    s_encoder_cache = ENC_CONEXANT;
  } else if (readByteSTOP(ENC_FOCUS, 0x00, dummy) == 0) {
    s_encoder_cache = ENC_FOCUS;
  } else if (readByteSTOP(ENC_XCALIBUR, 0x00, dummy) == 0) {
    s_encoder_cache = ENC_XCALIBUR;
  } else {
    s_encoder_cache = -1;
  }
  s_encoder_known = true;
}

// ===================== Public API =================
void SMBusExt::begin() {
  extUdp.begin(SMBUS_EXT_PORT);
  g_ext_first_ms = millis();
  g_ext_next_allowed_ms = g_ext_first_ms + SMBUS_EXT_STARTUP_GRACE_MS;
  s_xcal_next_probe_ms = g_ext_first_ms + SMBUS_EXT_STARTUP_GRACE_MS + 500; // first probe after grace
}

void SMBusExt::loop() {
  if (millis() < g_ext_next_allowed_ms) return;
  sendExtStatus();
}

void SMBusExt::sendExtStatus() {
  const uint32_t now = millis();

  // 1) Only one SMBus user at a time
  if (!try_lock_smbus()) {
    // small defer; let the poller keep its cadence
    g_ext_next_allowed_ms = now + SMBUS_EXT_MIN_PERIOD_MS;
    return;
  }

  Status packet;
  uint8_t b;
  bool ok = true;

  // 2) Base SMC fields (STOP-only). If any fail, SKIP transmit & back off.
  if (readByteSTOP(SMC_ADDRESS, SMC_TRAY, b) == 0) packet.trayState = (int)b;
  else { packet.trayState = -1; ok = false; }

  if (readByteSTOP(SMC_ADDRESS, SMC_AVSTATE, b) == 0) packet.avPackState = (int)b;
  else { packet.avPackState = -1; ok = false; }

  if (readByteSTOP(SMC_ADDRESS, SMC_VER, b) == 0) packet.picVer = (int)b;
  else { packet.picVer = -1; ok = false; }

  if (!ok) {
    uint32_t jitter = 150 + ((now & 0xFF) % 250); // 150..399ms
    g_ext_next_allowed_ms = now + SMBUS_EXT_BACKOFF_MS + jitter;
    unlock_smbus();
    return;
  }

  // 3) Console version policy
  int err = readByteSTOP(SMC_ADDRESS, SMC_CONSOLEVER, b);
  bool smcValid = (err == 0) && (b <= 6);
  if (smcValid) {
    packet.xboxVer = (int)b; // 0..6 direct
  } else {
    detectEncoderOnce();
    packet.xboxVer = (s_encoder_cache == ENC_XCALIBUR) ? 6 : -1;
  }

  // 4) Always broadcast encoder type if we know it
  if (!s_encoder_known) detectEncoderOnce();
  packet.encoderType = s_encoder_cache;

  // 5) Resolution (with safe Xcal mode probing on a timer)
  int width = -1, height = -1;

  if (s_encoder_cache == ENC_CONEXANT) {
    getConexantResolutionFromRegs(packet.avPackState, width, height);

  } else if (s_encoder_cache == ENC_FOCUS) {
    getFocusResolutionOrFallback(packet.avPackState, width, height);

  } else if (s_encoder_cache == ENC_XCALIBUR) {
    // Probe only once per XCAL_MODE_PROBE_PERIOD_MS (safe, single reg)
    maybeProbeXcalMode(now);
    if (s_xcal_mode_known && s_xcal_mode_code >= 0) {
      xcalCodeToWH((uint8_t)s_xcal_mode_code, packet.avPackState, width, height);
    } else {
      // Fallback if no successful probe yet
      xcalFallbackFromAv(packet.avPackState, width, height);
    }

  } else {
    // Unknown encoder: safest SD fallback
    xcalFallbackFromAv(packet.avPackState, width, height);
  }

  packet.videoWidth  = width;
  packet.videoHeight = height;

  // 6) Broadcast packet
  extUdp.beginPacket("255.255.255.255", SMBUS_EXT_PORT);
  extUdp.write((const uint8_t*)&packet, sizeof(packet));
  extUdp.endPacket();

#if SMBUS_EXT_DEBUG
  const char* encStr =
      s_encoder_cache == ENC_CONEXANT ? "CONEXANT" :
      s_encoder_cache == ENC_FOCUS    ? "FOCUS"    :
      s_encoder_cache == ENC_XCALIBUR ? "XCALIBUR" : "UNKNOWN";
  const char* smcStr = (err == 0) ? (String("0x") + String(b, HEX)).c_str() : "ERR";
  Serial.printf("[SMBusExt] EXT: Tray=%d AV=0x%02X PIC=0x%02X SMCverRaw=%s Enc=%s -> xboxVer=%d Res=%dx%d\n",
      packet.trayState,
      packet.avPackState & 0xFF,
      packet.picVer & 0xFF,
      smcStr,
      encStr,
      packet.xboxVer,
      packet.videoWidth, packet.videoHeight);
#endif

  // 7) Schedule next tick and release lock
  uint32_t jitter = 150 + ((now & 0xFF) % 250); // 150..399ms
  g_ext_next_allowed_ms = now + (smcValid ? SMBUS_EXT_MIN_PERIOD_MS
                                          : SMBUS_EXT_BACKOFF_MS) + jitter;
  unlock_smbus();
}
