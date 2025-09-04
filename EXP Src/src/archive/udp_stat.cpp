#include "udp_stat.h"
#include <WiFiUdp.h>
#include "cache_manager.h" // For XboxStatus
#include <WiFi.h>
#include "led_stat.h"

#define UDP_PORT 50504

static const uint8_t STATIC_ID = 6;         // Type D device ID
static const uint16_t ID_BROADCAST_PORT = 50502;
static unsigned long lastIdBroadcast = 0;
static const unsigned long idBroadcastInterval = 1000; // 1s interval for ID

static WiFiUDP udp;

static unsigned long lastCheck = 0; // For 5s polling
static const unsigned long checkInterval = 1000; // 5 seconds

// --- UDP blink state ---
static bool udpBlinking = false;
static unsigned long udpBlinkStart = 0;
static unsigned long lastBlink = 0;
static bool blinkState = false;

static const unsigned long blinkDuration = 2000; // ms: total time to blink orange (2 seconds)
static const unsigned long blinkPeriod = 150;    // ms: blink on/off cycle

// --- Replace this stub with your real data check ---
bool udpHasData() {
    // TODO: replace with your own check!
    // Return true if you have UDP data to send, else false.
    // For example:
    // return Cache_Manager::hasNewStatus();
    return true; // For testing: always blink. Replace this!
}

void sendUdpPacket() {
    const XboxStatus& st = Cache_Manager::getStatus();
    udp.beginPacket("255.255.255.255", UDP_PORT);
    udp.write(reinterpret_cast<const uint8_t*>(&st), sizeof(XboxStatus));
    udp.endPacket();
}

void UDPStat::begin() {
    Serial.printf("[UDPStat] UDP sender initialized on port %u\n", UDP_PORT);
}

void UDPStat::loop() {
    unsigned long now = millis();

    // 1. Every 5 seconds, check for UDP data
    if (now - lastCheck >= checkInterval) {
        lastCheck = now;
        if (udpHasData()) {
            // Send UDP and start blinking
            sendUdpPacket();
            udpBlinking = true;
            udpBlinkStart = now;
            lastBlink = now; // initialize blink timer
            blinkState = true; // start with LED on (orange)
            LedStat::setStatus(LedStatus::UdpTransmit);
        } else {
            udpBlinking = false; // not blinking
        }
    }

    // 2. If blinking, toggle LED every 150ms for 2 seconds
    if (udpBlinking) {
        if (now - udpBlinkStart >= blinkDuration) {
            udpBlinking = false;
            // 2 seconds over: revert to normal status
            if (WiFi.status() == WL_CONNECTED) {
                LedStat::setStatus(LedStatus::WifiConnected);
            } else {
                LedStat::setStatus(LedStatus::Portal);
            }
        } else {
            // Handle blinking
            if (now - lastBlink > blinkPeriod) {
                blinkState = !blinkState;
                LedStat::setStatus(blinkState ? LedStatus::UdpTransmit : LedStatus::WifiConnected);
                lastBlink = now;
            }
        }
        return;
    }

    // 3. ID broadcast at 1 Hz (unchanged)
    if (now - lastIdBroadcast >= idBroadcastInterval && WiFi.status() == WL_CONNECTED) {
        lastIdBroadcast = now;
        udp.beginPacket("255.255.255.255", ID_BROADCAST_PORT);
        udp.print("TYPE_D_ID:6");
        udp.endPacket();
    }

    // 4. If not blinking, keep LED showing connection status
    if (WiFi.status() == WL_CONNECTED) {
        LedStat::setStatus(LedStatus::WifiConnected);
    } else {
        LedStat::setStatus(LedStatus::Portal);
    }
}
