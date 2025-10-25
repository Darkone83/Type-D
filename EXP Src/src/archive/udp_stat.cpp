#include "udp_stat.h"
#include <WiFiUdp.h>
#include "cache_manager.h" // For XboxStatus
#include <WiFi.h>
#include "led_stat.h"
#include <string.h>
#include <Arduino.h>

// ---- exported by xbox_smbus_poll.cpp ----
extern uint32_t smbus_last_activity_ms();

// ====== Config ======
#define UDP_PORT                 50504
#define ID_BROADCAST_PORT        50502
#define UDP_STAT_DEBUG           0        // set 1 to enable Serial prints here

// How long the SMBus must be idle before we risk sending UDP
#ifndef SMBUS_QUIET_BEFORE_UDP_MS
#define SMBUS_QUIET_BEFORE_UDP_MS  6      // ~6 ms after last SMBus activity
#endif

// Main data send cadence (check for changes this often)
#ifndef UDP_CHECK_INTERVAL_MS
#define UDP_CHECK_INTERVAL_MS      5000   // ~5s
#endif

// Small jitter to avoid phase locking (0..JITTER_MAX_MS added to intervals)
#ifndef UDP_JITTER_MAX_MS
#define UDP_JITTER_MAX_MS          200
#endif

// ID beacon pacing
#ifndef ID_BROADCAST_INTERVAL_MS
#define ID_BROADCAST_INTERVAL_MS   1500   // ~1.5s nominal + jitter
#endif

// Blink timings
static const unsigned long blinkDuration = 2000; // blink for 2s when we send
static const unsigned long blinkPeriod   = 150;  // blink on/off cycle

// ====== State ======
static WiFiUDP udp;

static const uint8_t  STATIC_ID = 6; // Type D device ID

static unsigned long  nextDataCheck = 0;
static unsigned long  nextIdBeacon  = 0;

// --- UDP blink state ---
static bool           udpBlinking = false;
static unsigned long  udpBlinkStart = 0;
static unsigned long  lastBlink = 0;
static bool           blinkState = false;

// Keep last-sent to avoid spamming unchanged data
static XboxStatus g_last_sent = {0};

// ====== Helpers ======
static inline unsigned long jitter_ms(unsigned long maxJ) {
  return (millis() ^ 0xA5A5u) % (maxJ + 1);
}

static inline bool bus_quiet_enough() {
  const uint32_t last = smbus_last_activity_ms();
  const uint32_t now  = millis();
  // guard if last==0 (no activity yet) -> treat as quiet
  if (last == 0) return true;
  return (now - last) >= SMBUS_QUIET_BEFORE_UDP_MS;
}

static bool status_changed(const XboxStatus& a, const XboxStatus& b) {
  if (a.fanSpeed    != b.fanSpeed)    return true;
  if (a.cpuTemp     != b.cpuTemp)     return true;
  if (a.ambientTemp != b.ambientTemp) return true;
  if (strncmp(a.currentApp, b.currentApp, sizeof(a.currentApp)) != 0) return true;
  return false;
}

static bool udpHasData() {
  const XboxStatus& cur = Cache_Manager::getStatus();
  return status_changed(cur, g_last_sent);
}

static void sendUdpPacket() {
  const XboxStatus& st = Cache_Manager::getStatus();
  udp.beginPacket("255.255.255.255", UDP_PORT);
  udp.write(reinterpret_cast<const uint8_t*>(&st), sizeof(XboxStatus));
  udp.endPacket();
  g_last_sent = st; // mark as flushed
#if UDP_STAT_DEBUG
  Serial.println("[UDPStat] Sent status packet.");
#endif
}

// ====== Public ======
void UDPStat::begin() {
#if UDP_STAT_DEBUG
  Serial.printf("[UDPStat] UDP sender initialized on port %u\n", UDP_PORT);
#endif
  const unsigned long now = millis();
  nextDataCheck = now + UDP_CHECK_INTERVAL_MS + jitter_ms(UDP_JITTER_MAX_MS);
  nextIdBeacon  = now + ID_BROADCAST_INTERVAL_MS + jitter_ms(UDP_JITTER_MAX_MS);
}

void UDPStat::loop() {
  const unsigned long now = millis();

  // 1) If currently blinking to indicate a recent send, handle LED state.
  if (udpBlinking) {
    if (now - udpBlinkStart >= blinkDuration) {
      udpBlinking = false;
      // revert to normal status
      if (WiFi.status() == WL_CONNECTED) {
        LedStat::setStatus(LedStatus::WifiConnected);
      } else {
        LedStat::setStatus(LedStatus::Portal);
      }
    } else {
      if (now - lastBlink > blinkPeriod) {
        blinkState = !blinkState;
        LedStat::setStatus(blinkState ? LedStatus::UdpTransmit
                                      : LedStatus::WifiConnected);
        lastBlink = now;
      }
    }
    // (we continue so we can also do ID beacons while blinking)
  }

  // 2) Periodic status send (only when it changed AND bus is quiet)
  if (now >= nextDataCheck) {
    nextDataCheck = now + UDP_CHECK_INTERVAL_MS + jitter_ms(UDP_JITTER_MAX_MS);

    if (WiFi.status() == WL_CONNECTED && udpHasData()) {
      if (bus_quiet_enough()) {
        sendUdpPacket();
        // Start blink feedback (non-blocking)
        udpBlinking   = true;
        udpBlinkStart = now;
        lastBlink     = now;
        blinkState    = true;
        LedStat::setStatus(LedStatus::UdpTransmit);
      } else {
        // Defer a little if bus was busy—try again soon (but not immediately)
        nextDataCheck = now + 150 + jitter_ms(150);
#if UDP_STAT_DEBUG
        Serial.println("[UDPStat] Deferring data send (SMBus not quiet).");
#endif
      }
    }
  }

  // 3) ID beacon (lower duty, also bus-quiet aware)
  if (now >= nextIdBeacon) {
    nextIdBeacon = now + ID_BROADCAST_INTERVAL_MS + jitter_ms(UDP_JITTER_MAX_MS);

    if (WiFi.status() == WL_CONNECTED && bus_quiet_enough()) {
      udp.beginPacket("255.255.255.255", ID_BROADCAST_PORT);
      udp.print("TYPE_D_ID:");
      udp.print(STATIC_ID);
      udp.endPacket();
#if UDP_STAT_DEBUG
      Serial.println("[UDPStat] Sent ID beacon.");
#endif
    } else {
      // If bus is hot, try a little later
      nextIdBeacon = now + 300 + jitter_ms(200);
    }
  }

  // 4) Keep LED showing connection status when not blinking
  if (!udpBlinking) {
    if (WiFi.status() == WL_CONNECTED) {
      LedStat::setStatus(LedStatus::WifiConnected);
    } else {
      LedStat::setStatus(LedStatus::Portal);
    }
  }
}
