// Type_D_exp.ino
//
// Orchestrates SMBus polling, extended status, UDP sender, and one-shot EEPROM
// broadcast with strong bus-safety guarantees:
//
// - Each SMBus-using module (poller, smbus_ext, eeprom_min) uses the shared
//   global lock and its own pacing. We just trigger their .loop() methods.
// - Startup "grace" avoids poking the Xbox during boot.
// - EEPROM broadcast is one-shot, after grace + WiFi + first good poll.
// - Minimal serial prints to reduce timing noise.

#include "xbox_smbus_poll.h"
#include "parser_xboxsmbus.h"
#include "cache_manager.h"
#include "wifimgr.h"
#include "udp_stat.h"
#include "led_stat.h"
#include "smbus_ext.h"
#include "eeprom_min.h"
#include <Wire.h>

// ====== Hardware pins (set to your wiring) ======
#ifndef I2C_SDA_PIN
#define I2C_SDA_PIN 7
#endif
#ifndef I2C_SCL_PIN
#define I2C_SCL_PIN 6
#endif

// ====== Startup grace (don’t touch SMBus during Xbox boot) ======
#ifndef XBOX_BOOT_GRACE_MS
#define XBOX_BOOT_GRACE_MS 8000UL  // 8s default; override if you want
#endif

static unsigned long g_appStartMs = 0;

// ====== State ======
static XboxSMBusStatus g_smbusStatus;
static bool g_sawFirstGoodPoll = false;  // first successful poll gate for EEPROM
static bool g_eeSent = false;            // one-shot EEPROM broadcast sent?

void setup() {
  LedStat::begin();
  LedStat::setStatus(LedStatus::Booting);

  //Serial.begin(115200);
  //delay(150); // let USB CDC settle a bit

  WiFiMgr::begin();
  Cache_Manager::begin();

  // Initialize SMBus users; they manage Wire/pacing/locks themselves
  XboxSMBusPoll::begin(I2C_SDA_PIN, I2C_SCL_PIN);
  SMBusExt::begin();

  if (WiFiMgr::isConnected()) {
    UDPStat::begin();
  }

  g_appStartMs = millis();
  Serial.println("[Main] Type-D firmware started.");
}

void loop() {
  // lightweight background services
  LedStat::loop();
  WiFiMgr::loop();
  Cache_Manager::pollTitleUdp(); // Type-D app broadcaster

  const bool xboxReady = (millis() - g_appStartMs) >= XBOX_BOOT_GRACE_MS;

  // ===== SMBus temp/fan poller (module-paced & lock-protected) =====
  // We call it every loop; it will self-throttle and take the lock when safe.
  if (xboxReady) {
    if (XboxSMBusPoll::poll(g_smbusStatus)) {
      // Only update cache when a poll succeeds; avoids pushing stale/sentinel values.
      Cache_Manager::updateFromSmbus(g_smbusStatus);
      g_sawFirstGoodPoll = true;
    }
  }

  // ===== Extended status (AV pack / encoder / resolution) =====
  // Let it run independently; it does its own startup grace & pacing and uses the lock.
  SMBusExt::loop();

  // ===== One-shot EEPROM broadcast =====
  // - after startup grace
  // - after WiFi connected
  // - after at least one good poll (ensures the bus is alive)
  if (xboxReady && !g_eeSent && WiFiMgr::isConnected() && g_sawFirstGoodPoll) {
    XboxEEPROM::broadcastOnce();  // internal lock + STOP-only reads + one-shot
    g_eeSent = true;
  }

  // ===== UDP stats / ID beacons (no SMBus access here) =====
  if (WiFiMgr::isConnected()) {
    UDPStat::loop();
  }

  // ===== Modest status print every 5s =====
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 5000UL) {
    lastPrint = millis();
    const XboxStatus& st = Cache_Manager::getStatus();
    Serial.printf("[Main] Fan=%d%% CPU=%dC Amb=%dC App='%s'%s WiFi=%s\n",
                  st.fanSpeed, st.cpuTemp, st.ambientTemp, st.currentApp,
                  xboxReady ? "" : " (boot-grace)",
                  WiFiMgr::isConnected() ? "on" : "off");
  }

  // Light cooperative yield; lets WiFi/UDP run smoothly without jittering SMBus.
  // (Avoid long blocking work here; all bus work happens inside modules with pacing.)
  delay(1);
}
