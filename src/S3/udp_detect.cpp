#include "udp_detect.h"
#include <WiFiUdp.h>
#include "xbox_status.h"

#define UDP_PORT 50504

static WiFiUDP udp;
static XboxStatus lastStatus;
static bool gotPacket = false;

void UDPDetect::begin() {
    udp.begin(UDP_PORT);
    gotPacket = false;
    Serial.printf("[UDPDetect] Listening for XboxStatus packets on UDP port %u\n", UDP_PORT);
}

void UDPDetect::loop() {
    int packetSize = udp.parsePacket();
    if (packetSize == sizeof(XboxStatus)) {
        udp.read(reinterpret_cast<char*>(&lastStatus), sizeof(XboxStatus));
        gotPacket = true;
        Serial.printf("[UDPDetect] Packet received:\n");
        Serial.printf("  Fan: %d, CPU: %d, Ambient: %d, App: '%s'\n",
            lastStatus.fanSpeed, lastStatus.cpuTemp, lastStatus.ambientTemp, lastStatus.currentApp);
    } else if (packetSize > 0) {
        uint8_t tmp[packetSize];
        udp.read(tmp, packetSize);
    }
}

bool UDPDetect::hasPacket() {
    return gotPacket;
}

void UDPDetect::acknowledge() {
    gotPacket = false;
}

const XboxStatus& UDPDetect::getLatest() {
    return lastStatus;
}
