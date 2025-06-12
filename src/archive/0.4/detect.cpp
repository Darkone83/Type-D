#include <WiFi.h>
#include <WiFiUdp.h>
#include "detect.h"

// ==== CONFIGURABLES ====
#define DETECT_DISCOVER_PORT 50501
#define DETECT_BROADCAST_PORT 50502
#define DETECT_ID_MIN 1
#define DETECT_ID_MAX 4
#define DETECT_DISCOVER_TIMEOUT 1500   // ms to listen for replies after discover
#define DETECT_BROADCAST_INTERVAL 3000 // ms between ID broadcasts
#define DETECT_DISCOVER_MSG "TYPE_D_DISCOVER?"
#define DETECT_ID_MSG_PREFIX "TYPE_D_ID:"

namespace Detect {

uint8_t deviceId = 1;
uint32_t lastBroadcast = 0;
WiFiUDP udpDetect;
WiFiUDP udpBroadcast;
bool networkReady = false;
uint8_t knownIds[DETECT_ID_MAX + 1] = {0};

// ---- Helper: Check if WiFi/Ethernet is up ----
bool isNetworkReady() {
  return (WiFi.status() == WL_CONNECTED);
}

// ---- Serial debug helper ----
void debug(const char *msg) {
  Serial.print("[Detect] ");
  Serial.println(msg);
}

// ---- 1. Initialize module ----
void begin() {
  deviceId = 1;
  udpDetect.begin(DETECT_DISCOVER_PORT);
  udpBroadcast.begin(DETECT_BROADCAST_PORT);
  networkReady = isNetworkReady();
  if (networkReady) {
    debug("Network up, starting ID assignment.");
    assignId();
  } else {
    debug("No network. ID forced to 1.");
  }
}

// ---- 2. ID assignment process ----
void assignId() {
  memset(knownIds, 0, sizeof(knownIds));
  deviceId = 1; // fallback
  
  // Broadcast discovery
  udpDetect.beginPacket("255.255.255.255", DETECT_DISCOVER_PORT);
  udpDetect.write((const uint8_t *)DETECT_DISCOVER_MSG, strlen(DETECT_DISCOVER_MSG));
  udpDetect.endPacket();
  debug("Sent discover broadcast.");

  // Listen for replies (non-blocking loop for timeout window)
  uint32_t start = millis();
  while (millis() - start < DETECT_DISCOVER_TIMEOUT) {
    int packetSize = udpDetect.parsePacket();
    if (packetSize) {
      char buf[32] = {0};
      udpDetect.read(buf, sizeof(buf) - 1);
  if (strncmp(buf, DETECT_ID_MSG_PREFIX, strlen(DETECT_ID_MSG_PREFIX)) == 0) {
  int id = atoi(buf + strlen(DETECT_ID_MSG_PREFIX));
  IPAddress remoteIp = udpDetect.remoteIP();
  if (id == deviceId && remoteIp != WiFi.localIP()) {
    Serial.printf("[Detect] ID conflict! Saw my ID (%d) from %s. Reassigning...\n", id, remoteIp.toString().c_str());
    delay(200 + random(50, 250)); // Brief random delay to avoid storm
    assignId();
    }
  }

    }
    delay(5);
  }

  // Choose lowest unused ID
  for (uint8_t tryId = DETECT_ID_MIN; tryId <= DETECT_ID_MAX; ++tryId) {
    if (!knownIds[tryId]) {
      deviceId = tryId;
      break;
    }
  }
  Serial.printf("[Detect] Assigned ID: %d\n", deviceId);

  // Broadcast own ID so others can see
  char msg[32];
  snprintf(msg, sizeof(msg), DETECT_ID_MSG_PREFIX "%d", deviceId);
  udpDetect.beginPacket("255.255.255.255", DETECT_DISCOVER_PORT);
  udpDetect.write((const uint8_t *)msg, strlen(msg));
  udpDetect.endPacket();
  debug("Broadcasted my assigned ID.");
}

// ---- 3. Periodic ID broadcast on port 50502 ----
void broadcastId() {
  if (!networkReady) return;
  char msg[32];
  snprintf(msg, sizeof(msg), DETECT_ID_MSG_PREFIX "%d", deviceId);
  udpBroadcast.beginPacket("255.255.255.255", DETECT_BROADCAST_PORT);
  udpBroadcast.write((const uint8_t *)msg, strlen(msg));
  udpBroadcast.endPacket();
  Serial.printf("[Detect] Status broadcast: %s\n", msg);
}

// ---- 4. Simple conflict detection ----
void checkIdConflict() {
  // Listen on detect port for incoming "TYPE_D_ID:<id>" matching my ID but from a different device
  int packetSize = udpDetect.parsePacket();
  if (packetSize) {
    char buf[32] = {0};
    udpDetect.read(buf, sizeof(buf) - 1);
    if (strncmp(buf, DETECT_ID_MSG_PREFIX, strlen(DETECT_ID_MSG_PREFIX)) == 0) {
      int id = atoi(buf + strlen(DETECT_ID_MSG_PREFIX));
      IPAddress remoteIp = udpDetect.remoteIP();
      if (id == deviceId && remoteIp !=WiFi.localIP()) {
        Serial.printf("[Detect] ID conflict! Saw my ID (%d) from %s. Reassigning...\n", id, remoteIp.toString().c_str());
        delay(200 + random(50, 250)); // Brief random delay to avoid storm
        assignId();
      }
    }
  }
}

// ---- 5. Loop: call frequently in main loop ----
void loop() {
  // Update network status
  bool prev = networkReady;
  networkReady = isNetworkReady();

  if (networkReady && !prev) {
    debug("Network reconnected. Reassigning ID.");
    assignId();
  }
  if (!networkReady) {
    if (deviceId != 1) {
      deviceId = 1;
      debug("Lost network. Fallback to ID 1.");
    }
    return;
  }

  // Periodically broadcast ID for status
  if (millis() - lastBroadcast > DETECT_BROADCAST_INTERVAL) {
    lastBroadcast = millis();
    broadcastId();
  }

  // Conflict detection
  checkIdConflict();
}

uint8_t getId() { return deviceId; }

} // namespace Detect
