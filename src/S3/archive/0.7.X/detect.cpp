#include <WiFi.h>
#include <WiFiUdp.h>
#include "detect.h"

// ==== CONFIGURABLES ====
#define DETECT_DISCOVER_PORT 50501
#define DETECT_BROADCAST_PORT 50502
#define DETECT_ID_MIN 1
#define DETECT_ID_MAX 4
#define DETECT_DISCOVER_TIMEOUT 3000   // ms to listen for replies after discover
#define DETECT_PROPOSAL_WINDOW 2000    // ms to send repeated ID claim broadcasts
#define DETECT_PROPOSAL_BROADCAST_MS 200 // ms between claim broadcasts
#define DETECT_BROADCAST_INTERVAL 3000 // ms between regular status broadcasts
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

// ---- 2. Robust ID assignment with "proposal storm" ----
void assignId() {
  memset(knownIds, 0, sizeof(knownIds));
  deviceId = 1; // fallback

  delay(random(200, 800)); // jitter

  // ---- Discovery phase ----
  udpDetect.beginPacket("255.255.255.255", DETECT_DISCOVER_PORT);
  udpDetect.write((const uint8_t *)DETECT_DISCOVER_MSG, strlen(DETECT_DISCOVER_MSG));
  udpDetect.endPacket();
  debug("Sent discover broadcast.");

  uint32_t start = millis();
  while (millis() - start < DETECT_DISCOVER_TIMEOUT) {
    int packetSize = udpDetect.parsePacket();
    if (packetSize) {
      char buf[32] = {0};
      udpDetect.read(buf, sizeof(buf) - 1);
      IPAddress remoteIp = udpDetect.remoteIP();

      if (strcmp(buf, DETECT_DISCOVER_MSG) == 0) {
        char reply[32];
        snprintf(reply, sizeof(reply), DETECT_ID_MSG_PREFIX "%d", deviceId);
        udpDetect.beginPacket(remoteIp, DETECT_DISCOVER_PORT);
        udpDetect.write((const uint8_t *)reply, strlen(reply));
        udpDetect.endPacket();
      }
      if (strncmp(buf, DETECT_ID_MSG_PREFIX, strlen(DETECT_ID_MSG_PREFIX)) == 0) {
        int id = atoi(buf + strlen(DETECT_ID_MSG_PREFIX));
        if (id >= DETECT_ID_MIN && id <= DETECT_ID_MAX)
          knownIds[id] = 1;
      }
    }
    delay(5);
  }

  // Print known IDs for debug
  Serial.print("[Detect] knownIds after discovery: ");
  for (uint8_t i = DETECT_ID_MIN; i <= DETECT_ID_MAX; ++i)
    Serial.printf("%d:%d ", i, knownIds[i]);
  Serial.println();

  // --- Key Patch: If all IDs are marked as taken, still pick one deterministically
  // Find the first available ID. If all are taken, pick DETECT_ID_MAX as fallback.
  bool found = false;
  delay(random(100, 800)); // increased jitter to reduce collision!
  for (uint8_t tryId = DETECT_ID_MIN; tryId <= DETECT_ID_MAX; ++tryId) {
    if (!knownIds[tryId]) {
      deviceId = tryId;
      found = true;
      break;
    }
  }
  if (!found) {
    // All IDs marked as taken (should not happen, but possible in simultaneous boot) — pick last slot
    deviceId = DETECT_ID_MAX;
    Serial.println("[Detect] All IDs seen in use; defaulting to highest allowed ID!");
  }

  Serial.printf("[Detect] Proposing candidate ID: %d\n", deviceId);

  // ---- Proposal/claim storm phase ----
  char msg[32];
  snprintf(msg, sizeof(msg), DETECT_ID_MSG_PREFIX "%d", deviceId);

  bool seenConflict = false;
  uint32_t proposalStart = millis();
  while (millis() - proposalStart < DETECT_PROPOSAL_WINDOW) { // 2 second window
    // Broadcast our claim every 200ms
    udpDetect.beginPacket("255.255.255.255", DETECT_DISCOVER_PORT);
    udpDetect.write((const uint8_t *)msg, strlen(msg));
    udpDetect.endPacket();

    // Listen for conflicting claims for 200ms
    uint32_t listenUntil = millis() + DETECT_PROPOSAL_BROADCAST_MS;
    while (millis() < listenUntil) {
      int packetSize = udpDetect.parsePacket();
      if (packetSize) {
        char buf[32] = {0};
        udpDetect.read(buf, sizeof(buf) - 1);
        IPAddress remoteIp = udpDetect.remoteIP();
        if (strncmp(buf, DETECT_ID_MSG_PREFIX, strlen(DETECT_ID_MSG_PREFIX)) == 0) {
          int id = atoi(buf + strlen(DETECT_ID_MSG_PREFIX));
          if (id == deviceId && remoteIp != WiFi.localIP()) {
            // Collision detected!
            seenConflict = true;
            Serial.printf("[Detect] Proposal conflict! Saw my ID (%d) from %s. Will retry.\n", id, remoteIp.toString().c_str());
            break;
          }
        }
      }
      delay(5);
    }
    if (seenConflict) break;
  }

  if (seenConflict) {
    delay(random(300, 1200)); // backoff before retry
    Serial.println("[Detect] Conflict seen. Retrying assignment...");
    assignId();
    return;
  }

  // If we reach here, no conflicts were seen
  Serial.printf("[Detect] Final assigned ID: %d\n", deviceId);
  debug("ID assignment complete.");
}

// ---- 3. Periodic ID broadcast on port 50502 ----
void broadcastId() {
  if (!networkReady) return;
  char msg[32];
  snprintf(msg, sizeof(msg), DETECT_ID_MSG_PREFIX "%d", deviceId);
  udpBroadcast.beginPacket("255.255.255.255", DETECT_BROADCAST_PORT);
  udpBroadcast.write((const uint8_t *)msg, strlen(msg));
  udpBroadcast.endPacket();
}

// ---- 4. Conflict detection during runtime ----
void checkIdConflict() {
  int packetSize = udpDetect.parsePacket();
  if (packetSize) {
    char buf[32] = {0};
    udpDetect.read(buf, sizeof(buf) - 1);
    IPAddress remoteIp = udpDetect.remoteIP();
    Serial.printf("[Detect] (runtime conflict) Packet from %s: %s\n", remoteIp.toString().c_str(), buf);
    if (strncmp(buf, DETECT_ID_MSG_PREFIX, strlen(DETECT_ID_MSG_PREFIX)) == 0) {
      int id = atoi(buf + strlen(DETECT_ID_MSG_PREFIX));
      if (id == deviceId && remoteIp != WiFi.localIP()) {
        Serial.printf("[Detect] Runtime ID conflict! Saw my ID (%d) from %s. Will reassign.\n", id, remoteIp.toString().c_str());
        delay(random(300, 1000));
        assignId();
        return;
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
