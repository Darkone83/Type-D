// xbox_smbus_poll.cpp
//
// Safe, low-jitter, read-only SMBus poller for the Original Xbox.
// - STOP-only I2C phases by default (1.6-safe), RS is not required.
// - Cross-core FreeRTOS mutex so only one reader runs at a time.
// - Round-robin reads to avoid bursts (CPU, Board, Fan, Idle).
// - Slow bus (~55 kHz), explicit inter-op gaps, exponential backoff.
// - Light bus-idle observation (no internal pullups).
//
// Exports (for other modules like smbus_ext.cpp):
//   bool     try_lock_smbus();        // legacy wrapper on top of mutex
//   void     unlock_smbus();          // legacy wrapper on top of mutex
//   bool     smbus_acquire(uint32_t timeout_ms);
//   void     smbus_release();
//   uint32_t smbus_last_activity_ms();  // last time we touched the bus
//
// Notes:
// - This file is read-only on SMBus; there are NO writes to Xbox devices.
//

#include "xbox_smbus_poll.h"
#include "parser_xboxsmbus.h"
#include <Arduino.h>
#include <Wire.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// ---------- Xbox SMBus addresses / regs ----------
#define SMC_ADDRESS       0x10    // 7-bit
#define SMC_CPUTEMP       0x09
#define SMC_BOARDTEMP     0x0A
#define SMC_FANSPEED      0x10
#define XCALIBUR_ADDRESS  0x70    // Xbox 1.6 encoder (probe only, read)

// ---------- Timing & pacing (tuned for safety) ----------
#ifndef SMBUS_STARTUP_GRACE_MS
#define SMBUS_STARTUP_GRACE_MS    10000   // let the console boot first
#endif
#ifndef SMBUS_MIN_TICK_MS
#define SMBUS_MIN_TICK_MS          500   // one RR step every 1s (~4s full cycle)
#endif
#ifndef SMBUS_BACKOFF_MS_BASE
#define SMBUS_BACKOFF_MS_BASE      8000   // backoff base on error; exponential
#endif
#ifndef SMBUS_I2C_CLOCK_HZ
#define SMBUS_I2C_CLOCK_HZ         55000  // ~55 kHz, conservative for all revs
#endif
#ifndef SMBUS_INTER_OP_GAP_US
#define SMBUS_INTER_OP_GAP_US        300  // breathing room between phases
#endif
#ifndef SMBUS_WAIT_FREE_MS
#define SMBUS_WAIT_FREE_MS            15  // wait up to ~15ms for idle lines
#endif
#ifndef SMBUS_FREE_STABLE_CHECKS
#define SMBUS_FREE_STABLE_CHECKS       3  // consecutive samples required
#endif
#ifndef SMBUS_WIRE_TIMEOUT_MS
#define SMBUS_WIRE_TIMEOUT_MS          80 // allow for clock stretching
#endif
#ifndef SMBUS_16_DETECT_DELAY_MS
#define SMBUS_16_DETECT_DELAY_MS    12000 // don't probe Xcalibur until console is settled
#endif

// ---------- Global pacing state ----------
static uint32_t g_first_ms         = 0;
static uint32_t g_next_allowed_ms  = 0;
static uint8_t  g_err_streak       = 0;
static uint8_t  g_rr_step          = 0;

// Remember which pins Wire is on so we can observe the lines
static int s_sda_pin = -1;
static int s_scl_pin = -1;

// ---------- 1.6 detection cache (one-time) ----------
static bool g_is16_known  = false;
static bool g_is16_cached = false;

// ---------- Cross-core SMBus guard (mutex) ----------
// We keep legacy wrappers for compatibility with other files that may
// still call try_lock_smbus()/unlock_smbus().

static SemaphoreHandle_t g_smbus_mutex = nullptr;
static volatile uint32_t g_last_smbus_activity_ms = 0;

// Legacy variable kept for link compatibility (unused internally)
volatile bool g_smbus_locked = false;

static inline void mark_bus_activity() {
  g_last_smbus_activity_ms = millis();
}

// Exported helpers (can be declared in a small header if you like)
bool smbus_acquire(uint32_t timeout_ms) {
  if (!g_smbus_mutex) return false;
  if (xSemaphoreTake(g_smbus_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) {
    g_smbus_locked = true;  // legacy flag for old code paths
    return true;
  }
  return false;
}
void smbus_release() {
  if (g_smbus_mutex) {
    g_smbus_locked = false;
    xSemaphoreGive(g_smbus_mutex);
  }
}
uint32_t smbus_last_activity_ms() { return g_last_smbus_activity_ms; }

// Legacy wrappers (backed by the mutex)
bool try_lock_smbus() { return smbus_acquire(0); }
void unlock_smbus()   { smbus_release(); }

// ---------- Small helpers ----------
static inline void smbus_breather() {
  delayMicroseconds(SMBUS_INTER_OP_GAP_US);
  yield();
}

// Wait until both SDA & SCL are high for a few consecutive samples
static bool wait_bus_free(uint32_t max_wait_ms = SMBUS_WAIT_FREE_MS) {
  const uint32_t start = millis();
  int stable = 0;
  while ((millis() - start) < max_wait_ms) {
    const bool sdaHigh = (s_sda_pin >= 0) ? (digitalRead(s_sda_pin) == HIGH) : true;
    const bool sclHigh = (s_scl_pin >= 0) ? (digitalRead(s_scl_pin) == HIGH) : true;
    if (sdaHigh && sclHigh) {
      if (++stable >= SMBUS_FREE_STABLE_CHECKS) return true;
    } else {
      stable = 0;
    }
    delayMicroseconds(150);
  }
  return false;
}

// Very light recovery: only if the bus appears wedged for a while.
// (We do NOT spam re-inits; just one try per "wedged" observation.)
static void maybe_recover_wire() {
  static uint8_t stuck_streak = 0;
  if (!wait_bus_free()) {
    if (++stuck_streak >= 3) {
      Wire.begin(s_sda_pin, s_scl_pin);
      Wire.setClock(SMBUS_I2C_CLOCK_HZ);
      Wire.setTimeOut(SMBUS_WIRE_TIMEOUT_MS);
      stuck_streak = 0;
    }
  } else {
    stuck_streak = 0;
  }
}

// ---------- STOP-only single-byte read ----------
// Safer on 1.6: write(reg)+STOP, short gap, requestFrom()+STOP.
// Returns 0 on success, -1 on any failure.
static int readSMBusByteSTOP(uint8_t address, uint8_t reg, uint8_t& value) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  if (Wire.endTransmission(true) != 0) return -1;  // STOP
  smbus_breather();

  const uint8_t n = Wire.requestFrom((int)address, (int)1, (int)true); // STOP
  if (n != 1 || !Wire.available()) return -1;

  value = Wire.read();
  mark_bus_activity();
  smbus_breather();
  return 0;
}

// ---------- Public API ----------
void XboxSMBusPoll::begin(uint8_t sdaPin, uint8_t sclPin) {
  s_sda_pin = sdaPin;
  s_scl_pin = sclPin;

  // Do NOT enable internal pullups—Xbox SMBus already has pullups.
  pinMode(s_sda_pin, INPUT);
  pinMode(s_scl_pin, INPUT);

  Wire.begin(s_sda_pin, s_scl_pin);
  Wire.setClock(SMBUS_I2C_CLOCK_HZ);
  Wire.setTimeOut(SMBUS_WIRE_TIMEOUT_MS);

  if (!g_smbus_mutex) {
    g_smbus_mutex = xSemaphoreCreateMutex();
  }

  g_first_ms        = millis();
  g_next_allowed_ms = g_first_ms + SMBUS_STARTUP_GRACE_MS;
  g_err_streak      = 0;
  g_rr_step         = 0;
  g_is16_known      = false;
  g_is16_cached     = false;
}

bool XboxSMBusPoll::poll(XboxSMBusStatus& status) {
  const uint32_t now = millis();
  if (now < g_next_allowed_ms) return true;

  // Acquire the SMBus (non-blocking here; next tick will try again)
  if (!try_lock_smbus()) {
    g_next_allowed_ms = now + SMBUS_MIN_TICK_MS;
    return true;
  }

  bool ok = true;

  // Ensure the bus looks idle; attempt a single gentle recovery if not.
  if (!wait_bus_free()) {
    maybe_recover_wire();
    if (!wait_bus_free()) {
      ok = false;
    }
  }

  // One-shot 1.6 detection (safe, STOP-only) to the Xcalibur address,
  // but only after a post-boot delay to avoid early, fragile probing.
  bool is16 = g_is16_cached;
  if (ok && !g_is16_known && (now - g_first_ms) >= SMBUS_16_DETECT_DELAY_MS) {
    uint8_t dummy = 0;
    g_is16_cached = (readSMBusByteSTOP(XCALIBUR_ADDRESS, 0x00, dummy) == 0);
    g_is16_known  = true;        // mark known regardless to avoid repeat pokes
    is16 = g_is16_cached;
  }

  // Round-robin reading to avoid multi-read bursts per tick.
  if (ok) {
    uint8_t val = 0;
    switch (g_rr_step++ & 0x03) {
      case 0: { // CPU temp (C)
        if (readSMBusByteSTOP(SMC_ADDRESS, SMC_CPUTEMP, val) == 0 && val < 120) {
          status.cpuTemp = (int)val;
        } else {
          ok = false;
        }
        break;
      }
      case 1: { // Board temp (C), with 1.6 correction
        if (readSMBusByteSTOP(SMC_ADDRESS, SMC_BOARDTEMP, val) == 0 && val < 120) {
          if (is16) {
            // Tcorr_C ≈ 0.8*T_C − 3.56, rounded
            double f = (double)val * 1.8 + 32.0; // C -> F
            f *= 0.8;
            double c = (f - 32.0) / 1.8;         // F -> C
            int adj = (int)(c + (c >= 0.0 ? 0.5 : -0.5));
            if (adj < 0)   adj = 0;
            if (adj > 120) adj = 120;
            status.boardTemp = adj;
          } else {
            status.boardTemp = (int)val;
          }
        } else {
          ok = false;
        }
        break;
      }
      case 2: { // Fan speed (raw 0–50 → %)
        if (readSMBusByteSTOP(SMC_ADDRESS, SMC_FANSPEED, val) == 0 && val <= 50) {
          status.fanSpeed = (int)val * 2;
        } else {
          ok = false;
        }
        break;
      }
      case 3: { // Idle step to keep average bus pressure low
        // room for other modules (e.g., smbus_ext.cpp) to grab the bus
        break;
      }
    }
  }

  // Pacing / backoff
  if (!ok) {
    if (g_err_streak < 5) g_err_streak++;
    uint32_t backoff = (uint32_t)SMBUS_BACKOFF_MS_BASE << (g_err_streak - 1);
    if (backoff > 60000) backoff = 60000; // cap at 60s
    const uint32_t jitter = 100 + ((now & 0xFF) % 200); // 100..299 ms
    g_next_allowed_ms = now + backoff + jitter;
  } else {
    g_err_streak = 0;
    const uint32_t jitter = 100 + ((now & 0xFF) % 200); // 100..299 ms
    g_next_allowed_ms = now + SMBUS_MIN_TICK_MS + jitter;
  }

  unlock_smbus();
  return ok;
}
