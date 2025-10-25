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
// Focus FS454: same logic as you have, plus detailed debug of register reads.
// Enable with: #define SMBUS_EXT_DEBUG 1
static void getFocusResolutionOrFallback(int avVal, int& width, int& height) {
  width = -1; height = -1;

#if SMBUS_EXT_DEBUG
  static uint32_t s_focus_next_dbg_ms = 0;
  const uint32_t now_ms = millis();
  const bool do_dbg = (now_ms >= s_focus_next_dbg_ms);
  if (do_dbg) s_focus_next_dbg_ms = now_ms + 5000; // 5s throttle
#endif

  auto in = [](int v, int lo, int hi){ return v >= lo && v <= hi; };
  auto bswap = [](uint16_t x)->uint16_t { return (uint16_t)((x >> 8) | (x << 8)); };

  // Local helper: repeated-start, LSB-first (spi2par2019 style) -- used ONLY for debug + optional read
  auto readWordRS_LSB = [](uint8_t addr, uint8_t reg, uint16_t& out)->bool {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false; // repeated-start
    uint8_t n = Wire.requestFrom((int)addr, 2, (int)true); // read 2, STOP
    if (n != 2 || !Wire.available()) return false;
    const uint8_t lo = Wire.read();
    if (!Wire.available()) return false;
    const uint8_t hi = Wire.read();
    out = (uint16_t)lo | ((uint16_t)hi << 8); // LSB first
    return true;
  };

  // -------------------- 1) FS454 PID and VID_CNTL0 --------------------
  uint16_t pid_stop = 0, pid_rsl = 0;
  bool pid_ok_stop = (readWordSTOP(ENC_FOCUS, 0x32, pid_stop) == 0);
  bool pid_ok_rsl  = readWordRS_LSB(ENC_FOCUS, 0x32, pid_rsl);

  uint16_t vc0_stop = 0, vc0_rsl = 0;
  bool vc0_ok_stop = (readWordSTOP(ENC_FOCUS, 0x92, vc0_stop) == 0);
  bool vc0_ok_rsl  = readWordRS_LSB(ENC_FOCUS, 0x92, vc0_rsl);

#if SMBUS_EXT_DEBUG
  if (do_dbg) {
    Serial.printf("[FOCUS][DBG] PID stop=0x%04X swap=0x%04X rsl=0x%04X  (ok:%d/%d)\n",
                  pid_stop, bswap(pid_stop), pid_rsl, (int)pid_ok_stop, (int)pid_ok_rsl);
    Serial.printf("[FOCUS][DBG] VC0 stop=0x%04X swap=0x%04X rsl=0x%04X  (ok:%d/%d)\n",
                  vc0_stop, bswap(vc0_stop), vc0_rsl, (int)vc0_ok_stop, (int)vc0_ok_rsl);
  }
#endif

  // Prefer the spi2par2019 interpretation when available
  bool decided = false;
  if (pid_ok_rsl && pid_rsl == 0xFE05 && vc0_ok_rsl) {
    const bool HDTV       = (vc0_rsl & (1u << 12)) != 0;
    const bool INTERLACED = (vc0_rsl & (1u << 7))  != 0;
    if (HDTV) {
      width  = INTERLACED ? 1920 : 1280;
      height = INTERLACED ? 1080 : 720;
#if SMBUS_EXT_DEBUG
      if (do_dbg) Serial.printf("[FOCUS][DEC] via VC0.RSL HDTV=%d INT=%d => %dx%d\n",
                                (int)HDTV, (int)INTERLACED, width, height);
#endif
      return;
    } else {
      width = 720; height = 480;
#if SMBUS_EXT_DEBUG
      if (do_dbg) Serial.printf("[FOCUS][DEC] via VC0.RSL SD => %dx%d\n", width, height);
#endif
      return;
    }
  }

  // Fallback: try STOP+swap interpretation, just to see if that matches boards that wire MSB-first
  if (pid_ok_stop && bswap(pid_stop) == 0xFE05 && vc0_ok_stop) {
    const uint16_t VC0_SW = bswap(vc0_stop);
    const bool HDTV       = (VC0_SW & (1u << 12)) != 0;
    const bool INTERLACED = (VC0_SW & (1u << 7))  != 0;
    if (HDTV) {
      width  = INTERLACED ? 1920 : 1280;
      height = INTERLACED ? 1080 : 720;
#if SMBUS_EXT_DEBUG
      if (do_dbg) Serial.printf("[FOCUS][DEC] via VC0.STOP+SWAP HDTV=%d INT=%d => %dx%d\n",
                                (int)HDTV, (int)INTERLACED, width, height);
#endif
      return;
    } else {
      width = 720; height = 480;
#if SMBUS_EXT_DEBUG
      if (do_dbg) Serial.printf("[FOCUS][DEC] via VC0.STOP+SWAP SD => %dx%d\n", width, height);
#endif
      return;
    }
  }

  // -------------------- 2) HDTV ACTIVE window (HACT_WD, VACT_HT) --------------------
  uint16_t w_stop=0, h_stop=0, w_rsl=0, h_rsl=0;
  bool w_ok_stop = (readWordSTOP(ENC_FOCUS, 0xBA, w_stop) == 0);
  bool h_ok_stop = (readWordSTOP(ENC_FOCUS, 0xBE, h_stop) == 0);
  bool w_ok_rsl  = readWordRS_LSB(ENC_FOCUS, 0xBA, w_rsl);
  bool h_ok_rsl  = readWordRS_LSB(ENC_FOCUS, 0xBE, h_rsl);

#if SMBUS_EXT_DEBUG
  if (do_dbg) {
    Serial.printf("[FOCUS][DBG] HACT_WD stop=0x%04X (%4u)  rsl=0x%04X (%4u)  ok:%d/%d\n",
                  w_stop, (unsigned)(w_stop & 0x0FFF), w_rsl, (unsigned)(w_rsl & 0x0FFF),
                  (int)w_ok_stop, (int)w_ok_rsl);
    Serial.printf("[FOCUS][DBG] VACT_HT stop=0x%04X (%4u)  rsl=0x%04X (%4u)  ok:%d/%d\n",
                  h_stop, (unsigned)(h_stop & 0x0FFF), h_rsl, (unsigned)(h_rsl & 0x0FFF),
                  (int)h_ok_stop, (int)h_ok_rsl);
  }
#endif

  // Prefer RS_LSB values when available
  if (w_ok_rsl && h_ok_rsl) {
    const int W = (int)(w_rsl & 0x0FFF);
    const int H = (int)(h_rsl & 0x0FFF);
    if (in(H, 680, 760))        { width=1280; height=720;  decided=true; }
    else if (in(H, 1030,1120))  { width=1920; height=1080; decided=true; }
    else if (in(H, 460, 510))   { width = (in(W,1180,1380)?1280:720); height = (in(W,1180,1380)?720:480); decided=true; }
    else if (in(W,1180,1380) && in(H,620,820))   { width=1280; height=720;  decided=true; }
    else if (in(W,1840,1980) && in(H,980,1140))  { width=1920; height=1080; decided=true; }
    else if (in(W,690,780)   && in(H,430,530))   { width=720;  height=480;  decided=true; }

#if SMBUS_EXT_DEBUG
    if (do_dbg) {
      Serial.printf("[FOCUS][DEC] via ACTIVE.RSL W=%d H=%d => %s\n",
                    (int)(w_rsl & 0x0FFF), (int)(h_rsl & 0x0FFF),
                    decided ? "DECIDED" : "UNDECIDED");
    }
#endif
    if (decided) return;
  }

  // Try STOP words (use as-is and with swap) just to see if they look sane
  if (w_ok_stop && h_ok_stop) {
    const int W1 = (int)(w_stop & 0x0FFF), H1 = (int)(h_stop & 0x0FFF);
    const int W2 = (int)(bswap(w_stop) & 0x0FFF), H2 = (int)(bswap(h_stop) & 0x0FFF);
    // First try as-is
    if (!decided) {
      if (in(H1,680,760))        { width=1280; height=720;  decided=true; }
      else if (in(H1,1030,1120)) { width=1920; height=1080; decided=true; }
      else if (in(H1,460,510))   { width=(in(W1,1180,1380)?1280:720); height=(in(W1,1180,1380)?720:480); decided=true; }
      else if (in(W1,1180,1380) && in(H1,620,820))  { width=1280;height=720;  decided=true; }
      else if (in(W1,1840,1980) && in(H1,980,1140)) { width=1920;height=1080; decided=true; }
      else if (in(W1,690,780)   && in(H1,430,530))  { width=720; height=480;  decided=true; }
#if SMBUS_EXT_DEBUG
      if (do_dbg) Serial.printf("[FOCUS][DEC] via ACTIVE.STOP (as-is) W=%d H=%d => %s\n",
                                W1,H1, decided?"DECIDED":"UNDECIDED");
#endif
    }
    // Then try swapped
    if (!decided) {
      if (in(H2,680,760))        { width=1280; height=720;  decided=true; }
      else if (in(H2,1030,1120)) { width=1920; height=1080; decided=true; }
      else if (in(H2,460,510))   { width=(in(W2,1180,1380)?1280:720); height=(in(W2,1180,1380)?720:480); decided=true; }
      else if (in(W2,1180,1380) && in(H2,620,820))  { width=1280;height=720;  decided=true; }
      else if (in(W2,1840,1980) && in(H2,980,1140)) { width=1920;height=1080; decided=true; }
      else if (in(W2,690,780)   && in(H2,430,530))  { width=720; height=480;  decided=true; }
#if SMBUS_EXT_DEBUG
      if (do_dbg) Serial.printf("[FOCUS][DEC] via ACTIVE.STOP (swapped) W=%d H=%d => %s\n",
                                W2,H2, decided?"DECIDED":"UNDECIDED");
#endif
    }
    if (decided) return;
  }

  // -------------------- 3) SD counters (only if HD absent) --------------------
  uint16_t nl_stop=0, np_stop=0, nl_rsl=0, np_rsl=0;
  bool nl_ok_stop = (readWordSTOP(ENC_FOCUS, 0x57, nl_stop) == 0);
  bool np_ok_stop = (readWordSTOP(ENC_FOCUS, 0x71, np_stop) == 0);
  bool nl_ok_rsl  = readWordRS_LSB(ENC_FOCUS, 0x57, nl_rsl);
  bool np_ok_rsl  = readWordRS_LSB(ENC_FOCUS, 0x71, np_rsl);

#if SMBUS_EXT_DEBUG
  if (do_dbg) {
    Serial.printf("[FOCUS][DBG] NL stop=0x%04X(%4u) swap=%4u rsl=0x%04X(%4u) ok:%d/%d\n",
                  nl_stop, (unsigned)(nl_stop & 0x07FF), (unsigned)(bswap(nl_stop) & 0x07FF),
                  nl_rsl, (unsigned)(nl_rsl & 0x07FF), (int)nl_ok_stop, (int)nl_ok_rsl);
    Serial.printf("[FOCUS][DBG] NP stop=0x%04X(%4u) swap=%4u rsl=0x%04X(%4u) ok:%d/%d\n",
                  np_stop, (unsigned)(np_stop & 0x07FF), (unsigned)(bswap(np_stop) & 0x07FF),
                  np_rsl, (unsigned)(np_rsl & 0x07FF), (int)np_ok_stop, (int)np_ok_rsl);
  }
#endif

  // Use RS_LSB SD values first, else STOP as-is, else STOP swapped
  if (nl_ok_rsl && np_ok_rsl) {
    height = (int)(nl_rsl & 0x07FF);
    width  = (int)(np_rsl & 0x07FF);
#if SMBUS_EXT_DEBUG
    if (do_dbg) Serial.printf("[FOCUS][DEC] via SD.RSL => %dx%d\n", width, height);
#endif
    return;
  }
  if (nl_ok_stop && np_ok_stop) {
    height = (int)(nl_stop & 0x07FF);
    width  = (int)(np_stop & 0x07FF);
#if SMBUS_EXT_DEBUG
    if (do_dbg) Serial.printf("[FOCUS][DEC] via SD.STOP(as-is) => %dx%d\n", width, height);
#endif
    return;
  }
  if (nl_ok_stop || np_ok_stop) { // mixed case
    height = nl_ok_stop ? (int)(bswap(nl_stop) & 0x07FF) : -1;
    width  = np_ok_stop ? (int)(bswap(np_stop) & 0x07FF) : -1;
    if (width > 0 && height > 0) {
#if SMBUS_EXT_DEBUG
      if (do_dbg) Serial.printf("[FOCUS][DEC] via SD.STOP(swapped) => %dx%d\n", width, height);
#endif
      return;
    }
  }

  // -------------------- 4) AV-pack guard --------------------
  const bool pal = isPalFromAvPack(avVal);
  width = 720; height = pal ? 576 : 480;
#if SMBUS_EXT_DEBUG
  if (do_dbg) Serial.printf("[FOCUS][DEC] via AV-PACK => %dx%d\n", width, height);
#endif
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
