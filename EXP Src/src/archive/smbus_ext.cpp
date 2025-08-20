#include "smbus_ext.h"
#include <WiFiUdp.h>
#include <Wire.h>

#define SMBUS_EXT_PORT 50505

// Addresses
#define SMC_ADDRESS    0x10

// Video encoders (probe order)
#define ENC_CONEXANT 0x45
#define ENC_FOCUS    0x6A
#define ENC_XCALIBUR 0x70

// SMC registers
#define SMC_TRAY       0x03
#define SMC_AVSTATE    0x04
#define SMC_VER        0x01
#define SMC_CONSOLEVER 0x00   // Often returns 0xFF (not reported)

// ---------- UDP ----------
static WiFiUDP extUdp;

// ---------- SMBus helpers ----------
static int readSMBusByte(uint8_t address, uint8_t reg, uint8_t& value) {
    Wire.beginTransmission(address);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return -1;       // repeated start
    uint8_t n = Wire.requestFrom(address, (uint8_t)1);
    if (n == 1 && Wire.available()) { value = Wire.read(); return 0; }
    return -1;
}

static int readSMBus16(uint8_t address, uint8_t reg, uint16_t& value) {
    Wire.beginTransmission(address);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return -1;
    uint8_t n = Wire.requestFrom(address, (uint8_t)2);
    if (n == 2 && Wire.available() >= 2) {
        uint8_t msb = Wire.read(), lsb = Wire.read();
        value = ((uint16_t)msb << 8) | lsb;
        return 0;
    }
    return -1;
}

// ---------- Encoder detect ----------
static int detectEncoder() {
    uint8_t dummy;
    if (readSMBusByte(ENC_CONEXANT, 0x00, dummy) == 0) return ENC_CONEXANT;
    if (readSMBusByte(ENC_FOCUS,    0x00, dummy) == 0) return ENC_FOCUS;
    if (readSMBusByte(ENC_XCALIBUR, 0x00, dummy) == 0) return ENC_XCALIBUR;
    return -1;
}

// Map encoder → best-effort Xbox HW “family”
static uint8_t deriveXboxFamilyFromEncoder(int encoderAddr) {
    switch (encoderAddr) {
        case ENC_XCALIBUR: return 0x06; // v1.6
        case ENC_FOCUS:    return 0x05; // v1.4/1.5 bucket
        case ENC_CONEXANT: return 0x03; // v1.0–1.3 bucket
        default:           return 0xFF; // unknown
    }
}

// Heuristic: does AV pack look PAL/SCART-ish?
// Primary scheme: 0x00=SCART; fallback even-nibble: 0x0E=SCART.
static bool isPalFromAvPack(int avVal) {
    int v = avVal & 0xFF;
    if (v == 0x00) return true;          // SCART (primary table)
    if ((v & 0x0E) == 0x0E) return true; // SCART (fallback table)
    return false;
}

// Decode resolution for Conexant CX25870/871 (0x45):
// Reg 0x2E: D7=HDTV_EN, D1..D0=RASTER_SEL (01=480p, 10=720p, 11=1080i)
static void getConexantResolution(const SMBusExt::Status& pktIn, int& width, int& height) {
    width = -1; height = -1;

    uint8_t r2e = 0;
    if (readSMBusByte(ENC_CONEXANT, 0x2E, r2e) == 0) {
        bool hdtv = (r2e & 0x80) != 0;
        uint8_t ras = (r2e & 0x03);

        if (hdtv) {
            switch (ras) {
                case 0x01: width = 720;   height = 480;  break; // 480p
                case 0x02: width = 1280;  height = 720;  break; // 720p
                case 0x03: width = 1920;  height = 1080; break; // 1080i
                default: /* 00 = external timing */ break;
            }
        } else {
            // SDTV mode: pick PAL vs NTSC based on AV pack
            bool pal = isPalFromAvPack(pktIn.avPackState);
            width  = 720;
            height = pal ? 576 : 480;
        }
    } else {
        // Couldn’t read reg; provide SD fallback so UI shows something usable
        bool pal = isPalFromAvPack(pktIn.avPackState);
        width  = 720;
        height = pal ? 576 : 480;
    }
}

void SMBusExt::begin() {
    extUdp.begin(SMBUS_EXT_PORT);
}

void SMBusExt::loop() {
    static unsigned long last = 0;
    if (millis() - last > 2000) { last = millis(); sendExtStatus(); }
}

void SMBusExt::sendExtStatus() {
    Status packet;
    uint8_t b;

    // Base SMC fields
    packet.trayState   = (readSMBusByte(SMC_ADDRESS, SMC_TRAY, b) == 0) ? b : -1;
    packet.avPackState = (readSMBusByte(SMC_ADDRESS, SMC_AVSTATE, b) == 0) ? b : -1;
    packet.picVer      = (readSMBusByte(SMC_ADDRESS, SMC_VER, b) == 0) ? b : -1;

    // Encoder detect (cache result)
    static int encoder = -2;
    if (encoder == -2) encoder = detectEncoder();
    packet.encoderType = encoder;

    // Console/Xbox version:
    // - Try SMC 0x00; many boards return 0xFF → not reported
    // - If not reported, derive a coarse family from encoder
    int err = readSMBusByte(SMC_ADDRESS, SMC_CONSOLEVER, b);
    if (err == 0 && b != 0xFF && b != 0x00) {
        packet.xboxVer = b;
    } else {
        uint8_t guess = deriveXboxFamilyFromEncoder(encoder);
        packet.xboxVer = (guess == 0xFF) ? -1 : guess;
    }

    // Resolution
    int width = -1, height = -1;

    if (encoder == ENC_CONEXANT) {
        // Conexant: decode via 0x2E (HDTV_EN + RASTER_SEL) with PAL/NTSC SD fallback
        getConexantResolution(packet, width, height);
    } else if (encoder == ENC_FOCUS) {
        // Focus FS454: try active area first, then NUM_PIXELS / NUM_LINES
        uint16_t hact, vact;
        if (readSMBus16(ENC_FOCUS, 0xBA, hact) == 0) width  = (int)(hact & 0x0FFF); // HACT_WD
        if (readSMBus16(ENC_FOCUS, 0xBE, vact) == 0) height = (int)(vact & 0x0FFF); // VACT_HT
        if (width  <= 0) { uint16_t np; if (readSMBus16(ENC_FOCUS, 0x71, np) == 0) width  = (int)(np & 0x07FF); }
        if (height <= 0) { uint16_t nl; if (readSMBus16(ENC_FOCUS, 0x57, nl) == 0) height = (int)(nl & 0x07FF); }
        // As a final fallback, supply SD PAL/NTSC
        if (width <= 0 || height <= 0) {
            bool pal = isPalFromAvPack(packet.avPackState);
            width = 720; height = pal ? 576 : 480;
        }
    } else if (encoder == ENC_XCALIBUR || encoder == -1) {
        // Unknown/Xcalibur: provide SD PAL/NTSC fallback
        bool pal = isPalFromAvPack(packet.avPackState);
        width = 720; height = pal ? 576 : 480;
    }

    packet.videoWidth  = width;
    packet.videoHeight = height;

    // Broadcast
    extUdp.beginPacket("255.255.255.255", SMBUS_EXT_PORT);
    extUdp.write((const uint8_t*)&packet, sizeof(packet));
    extUdp.endPacket();

    Serial.printf("[SMBusExt] EXT: Tray=%d AV=%d Ver=%d XboxVer=%d Enc=0x%02X Res=%dx%d\n",
        packet.trayState, packet.avPackState, packet.picVer,
        packet.xboxVer, packet.encoderType, packet.videoWidth, packet.videoHeight);
}

void SMBusExt::sendCustomStatus(const Status& status) {
    extUdp.beginPacket("255.255.255.255", SMBUS_EXT_PORT);
    extUdp.write((const uint8_t*)&status, sizeof(status));
    extUdp.endPacket();

    Serial.printf("[SMBusExt] CUSTOM EXT: Tray=%d AV=%d Ver=%d XboxVer=%d Enc=0x%02X Res=%dx%d\n",
        status.trayState, status.avPackState, status.picVer,
        status.xboxVer, status.encoderType, status.videoWidth, status.videoHeight);
}
