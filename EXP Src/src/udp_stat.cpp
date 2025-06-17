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
static unsigned long lastSend = 0;
static const unsigned long sendInterval = 100; // 10 Hz

void UDPStat::begin() {
    Serial.printf("[UDPStat] UDP sender initialized on port %u\n", UDP_PORT);
}

void UDPStat::loop() {
    unsigned long now = millis();

    // 1. Status broadcast at 10 Hz
    if (now - lastSend >= sendInterval) {
        lastSend = now;

        if (WiFi.status() == WL_CONNECTED) {
            const XboxStatus& st = Cache_Manager::getStatus();

            // Set LED to transmitting (orange) just before send
            LedStat::setStatus(LedStatus::UdpTransmit);

            Serial.printf("[UDPStat] Sending XboxStatus packet to 255.255.255.255:%u\n", UDP_PORT);
            Serial.printf("  Fan: %d, CPU: %d, Ambient: %d, App: '%s'\n",
                st.fanSpeed, st.cpuTemp, st.ambientTemp, st.currentApp);

            udp.beginPacket("255.255.255.255", UDP_PORT);
            udp.write(reinterpret_cast<const uint8_t*>(&st), sizeof(XboxStatus));
            udp.endPacket();

            Serial.println("[UDPStat] Packet sent.");

            // (LED status can remain 'UdpTransmit' until end of loop for visual blink)
        } else {
            Serial.println("[UDPStat] Not connected to WiFi, skipping send.");
            LedStat::setStatus(LedStatus::Portal);
        }
    }

    // 2. ID broadcast at 1 Hz
    if (now - lastIdBroadcast >= idBroadcastInterval && WiFi.status() == WL_CONNECTED) {
        lastIdBroadcast = now;

        udp.beginPacket("255.255.255.255", ID_BROADCAST_PORT);
        udp.write(&STATIC_ID, 1); // Send single byte: 6
        udp.endPacket();
        Serial.printf("[UDPStat] Broadcasted ID %u to port %u\n", STATIC_ID, ID_BROADCAST_PORT);
    }

    // 3. Set LED state after transmitting (for a visible blink)
    // (You may want to keep UdpTransmit for X ms, or just set to green at the end)
    if (WiFi.status() == WL_CONNECTED) {
        LedStat::setStatus(LedStatus::WifiConnected);
    } else {
        LedStat::setStatus(LedStatus::Portal);
    }
}

