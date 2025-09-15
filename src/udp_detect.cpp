#include "udp_detect.h"
#include <WiFiUdp.h>
#include "xbox_status.h"
#include <cstring>  // memcpy, strncpy

#define UDP_PORT_CORE   50504
#define UDP_PORT_EXP    50505

static WiFiUDP udpCore;
static WiFiUDP udpExp;

static XboxStatus lastStatus;
static bool gotPacket = false;

// --- Wire format for core telemetry (50504) ---
struct CorePacket {
    int32_t fanSpeed;
    int32_t cpuTemp;
    int32_t ambientTemp;
    char    currentApp[32];
};

// Pretty print resolution like the PC viewer
static void formatResolution(int w, int h, char *out, size_t outSize) {
    if (!out || outSize == 0) return;
    String val = String(w) + "x" + String(h);
    if (w == 640  && h == 480)  val += " (480p)";
    else if (w == 720  && h == 480)  val += " (480p WS)";
    else if (w == 720  && h == 576)  val += " (576i/p)";
    else if (w == 1280 && h == 720)  val += " (720p)";
    else if (w == 1920 && h == 1080) val += " (1080i)";
    strncpy(out, val.c_str(), outSize - 1);
    out[outSize - 1] = '\0';
}

void UDPDetect::begin() {
    udpCore.begin(UDP_PORT_CORE);
    udpExp.begin(UDP_PORT_EXP);
    gotPacket = false;
    Serial.printf("[UDPDetect] Listening on %u (core) and %u (expansion)\n",
                  UDP_PORT_CORE, UDP_PORT_EXP);
}

void UDPDetect::loop() {
    // --- Core telemetry (Fan/CPU/Ambient/App) ---
    int sz = udpCore.parsePacket();
    if (sz == (int)sizeof(CorePacket)) {
        CorePacket cp;
        int n = udpCore.read(reinterpret_cast<char*>(&cp), sizeof(cp));
        if (n == (int)sizeof(cp)) {
            lastStatus.fanSpeed    = cp.fanSpeed;
            lastStatus.cpuTemp     = cp.cpuTemp;
            lastStatus.ambientTemp = cp.ambientTemp;
            strncpy(lastStatus.currentApp, cp.currentApp, sizeof(lastStatus.currentApp) - 1);
            lastStatus.currentApp[sizeof(lastStatus.currentApp) - 1] = '\0';

            gotPacket = true;
            Serial.printf("[UDPDetect] Core: Fan=%d, CPU=%d, Amb=%d, App='%s'\n",
                          lastStatus.fanSpeed, lastStatus.cpuTemp,
                          lastStatus.ambientTemp, lastStatus.currentApp);
        } else {
            uint8_t tmp[sz]; udpCore.read(tmp, sz);
        }
    } else if (sz > 0) {
        uint8_t tmp[sz]; udpCore.read(tmp, sz);
    }

    // --- Expansion telemetry (7 x int32_t, little-endian) ---
    sz = udpExp.parsePacket();
    if (sz == 7 * (int)sizeof(int32_t)) {
        uint8_t buf[28];
        int n = udpExp.read(buf, sizeof(buf));
        if (n == (int)sizeof(buf)) {
            // ACTUAL ORDER (fix): tray, av, pic, xboxver, width, height, encoder
            int32_t tray, av, pic, xboxver, width, height, encoder;
            memcpy(&tray,    buf +  0, 4);
            memcpy(&av,      buf +  4, 4);
            memcpy(&pic,     buf +  8, 4);
            memcpy(&xboxver, buf + 12, 4);
            memcpy(&width,   buf + 16, 4);   // width is 5th
            memcpy(&height,  buf + 20, 4);   // height is 6th
            memcpy(&encoder, buf + 24, 4);   // encoder is LAST

            lastStatus.trayState   = tray;
            lastStatus.avPack      = av;
            lastStatus.picVersion  = pic;
            lastStatus.xboxVersion = xboxver;
            lastStatus.videoWidth  = width;
            lastStatus.videoHeight = height;
            lastStatus.encoder     = encoder;   // 0x45 Conexant, 0x6A Focus, 0x70 Xcalibur

            formatResolution(width, height, lastStatus.resolution, sizeof(lastStatus.resolution));

            gotPacket = true;
            Serial.printf("[UDPDetect] Exp: Tray=%d, AV=%d, PIC=%d, XboxVer=%d, Encoder=%d, Res=%s\n",
                          tray, av, pic, xboxver, encoder, lastStatus.resolution);
        } else {
            uint8_t tmp[sz]; udpExp.read(tmp, sz);
        }
    } else if (sz > 0) {
        uint8_t tmp[sz]; udpExp.read(tmp, sz);
    }
}

bool UDPDetect::hasPacket() { return gotPacket; }
void UDPDetect::acknowledge() { gotPacket = false; }
const XboxStatus& UDPDetect::getLatest() { return lastStatus; }
