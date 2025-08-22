#include "smbus_ext.h"
#include <WiFiUdp.h>
#include <Wire.h>

// ===================== Config =====================
#define SMBUS_EXT_PORT 50505

// SMBus / I2C addresses
#define SMC_ADDRESS    0x10

// Video encoders (device addresses)
#define ENC_CONEXANT 0x45
#define ENC_FOCUS    0x6A
#define ENC_XCALIBUR 0x70

// SMC registers
#define SMC_TRAY       0x03
#define SMC_AVSTATE    0x04
#define SMC_VER        0x01
#define SMC_CONSOLEVER 0x00   // may be 0xFF (not reported on some boards)

// ===================== UDP ========================
static WiFiUDP extUdp;

// ===================== SMBus helpers ==============

static int readSMBusByte(uint8_t address, uint8_t reg, uint8_t& value) {
    Wire.beginTransmission(address);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return -1; // repeated-start
    uint8_t n = Wire.requestFrom((int)address, 1, (int)true);
    if (n == 1 && Wire.available()) { value = Wire.read(); return 0; }
    return -1;
}

// Some SMC firmwares don’t like repeated-start.
// Try normal (repeated-start) first, then retry with STOP between ops.
static int readSMBusByteCompat(uint8_t address, uint8_t reg, uint8_t& value) {
    // Attempt 1: repeated-start
    if (readSMBusByte(address, reg, value) == 0) return 0;

    // Attempt 2: STOP between write & read
    Wire.beginTransmission(address);
    Wire.write(reg);
    if (Wire.endTransmission(true) != 0) return -1;
    uint8_t n = Wire.requestFrom((int)address, 1, (int)true);
    if (n == 1 && Wire.available()) { value = Wire.read(); return 0; }

    return -1;
}

static int readSMBus16(uint8_t address, uint8_t reg, uint16_t& value) {
    Wire.beginTransmission(address);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return -1;
    uint8_t n = Wire.requestFrom((int)address, 2, (int)true);
    if (n == 2 && Wire.available() >= 2) {
        uint8_t msb = Wire.read(), lsb = Wire.read();
        value = ((uint16_t)msb << 8) | lsb;
        return 0;
    }
    return -1;
}

// ===================== Encoder detect =============

static int detectEncoder() {
    uint8_t dummy;
    if (readSMBusByte(ENC_CONEXANT, 0x00, dummy) == 0) return ENC_CONEXANT;
    if (readSMBusByte(ENC_FOCUS,    0x00, dummy) == 0) return ENC_FOCUS;
    if (readSMBusByte(ENC_XCALIBUR, 0x00, dummy) == 0) return ENC_XCALIBUR;
    return -1;
}

// ===================== AV-pack heuristics =========

static bool isPalFromAvPack(int avVal) {
    int v = avVal & 0xFF;
    if (v == 0x00) return true;          // SCART (primary table)
    if ((v & 0x0E) == 0x0E) return true; // SCART (fallback even-nibble)
    return false;
}

// ===================== Conexant resolution ========

static void getConexantResolutionFromRegs(int avVal, int& width, int& height) {
    width = -1; height = -1;

    // Conexant: HDTV_EN (bit7) and RASTER_SEL (bits1..0) @ 0x2E
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
        }
    }

    if (width <= 0 || height <= 0) {
        // SD fallback from AV pack
        bool pal = isPalFromAvPack(avVal);
        width  = 720;
        height = pal ? 576 : 480;
    }
}

// ===================== Xcalibur mode probe =========
// Treat low 3 bits as 0..5 mode code (0:480i, 1:480p, 2:576i, 3:576p, 4:720p, 5:1080i)
static bool tryReadXcaliburModeCode(uint8_t& outCode, uint8_t& outReg) {
    const uint8_t regs[] = { 0x1C, 0x07, 0x02 }; // heuristic candidates
    for (size_t i = 0; i < sizeof(regs); ++i) {
        uint8_t val = 0;
        if (readSMBusByte(ENC_XCALIBUR, regs[i], val) == 0) {
            // Debug (can disable after confirmation)
            Serial.printf("[SMBusExt] Xcal read reg 0x%02X = 0x%02X\n", regs[i], val);
            uint8_t code = (val & 0x07);
            if (code <= 5) { outCode = code; outReg = regs[i]; return true; }
        }
    }
    return false;
}

static void getXcaliburResolutionFromCodeOrFallback(int avVal, int& width, int& height) {
    width = -1; height = -1;
    uint8_t code = 0, reg = 0;

    if (tryReadXcaliburModeCode(code, reg)) {
        switch (code) {
            case 0: width = 720;  height = 480;  break; // 480i
            case 1: width = 720;  height = 480;  break; // 480p
            case 2: width = 720;  height = 576;  break; // 576i
            case 3: width = 720;  height = 576;  break; // 576p
            case 4: width = 1280; height = 720;  break; // 720p
            case 5: width = 1920; height = 1080; break; // 1080i
            default: break;
        }
        if (width > 0 && height > 0) {
            Serial.printf("[SMBusExt] Xcal mode code %u (reg 0x%02X) -> %dx%d\n",
                          code, reg, width, height);
            return;
        }
    }

    // Fallback: PAL/NTSC SD from AV pack
    bool pal = isPalFromAvPack(avVal);
    width  = 720;
    height = pal ? 576 : 480;
    Serial.printf("[SMBusExt] Xcal fallback -> %dx%d (from AV pack)\n", width, height);
}

// ===================== Public API =================

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

    // -------- Base SMC fields (compat read) --------
    packet.trayState   = (readSMBusByteCompat(SMC_ADDRESS, SMC_TRAY, b) == 0) ? (int)b : -1;
    packet.avPackState = (readSMBusByteCompat(SMC_ADDRESS, SMC_AVSTATE, b) == 0) ? (int)b : -1;
    packet.picVer      = (readSMBusByteCompat(SMC_ADDRESS, SMC_VER, b) == 0) ? (int)b : -1;

    // -------- Encoder detect (cache result) --------
    static int encoder = -2;
    if (encoder == -2) encoder = detectEncoder();
    packet.encoderType = encoder;

    // -------- Console version policy ---------------
    // Prefer SMC 0x00 if valid 0..6.
    // If not valid:
    //   - Xcalibur => 6 (v1.6 is definitive)
    //   - Focus/Conexant => -1 (unknown); PC app will bucket safely
    int err = readSMBusByteCompat(SMC_ADDRESS, SMC_CONSOLEVER, b);
    bool smcValid = (err == 0) && (b <= 6);
    if (smcValid) {
        packet.xboxVer = (int)b; // exact 0..6
    } else {
        if (encoder == ENC_XCALIBUR) packet.xboxVer = 6;
        else                         packet.xboxVer = -1; // let viewer bucket
    }

    // -------- Resolution ---------------------------
    int width = -1, height = -1;
    if (encoder == ENC_CONEXANT) {
        getConexantResolutionFromRegs(packet.avPackState, width, height);
    } else if (encoder == ENC_FOCUS) {
        // Focus FS454: active area, then fallbacks
        uint16_t hact = 0, vact = 0;
        if (readSMBus16(ENC_FOCUS, 0xBA, hact) == 0) width  = (int)(hact & 0x0FFF); // HACT_WD
        if (readSMBus16(ENC_FOCUS, 0xBE, vact) == 0) height = (int)(vact & 0x0FFF); // VACT_HT
        if (width  <= 0) { uint16_t np = 0; if (readSMBus16(ENC_FOCUS, 0x71, np) == 0) width  = (int)(np & 0x07FF); }
        if (height <= 0) { uint16_t nl = 0; if (readSMBus16(ENC_FOCUS, 0x57, nl) == 0) height = (int)(nl & 0x07FF); }
        if (width <= 0 || height <= 0) {
            bool pal = isPalFromAvPack(packet.avPackState);
            width = 720; height = pal ? 576 : 480;
        }
    } else if (encoder == ENC_XCALIBUR) {
        getXcaliburResolutionFromCodeOrFallback(packet.avPackState, width, height);
    } else {
        bool pal = isPalFromAvPack(packet.avPackState);
        width = 720; height = pal ? 576 : 480;
    }

    packet.videoWidth  = width;
    packet.videoHeight = height;

    // -------- Broadcast packet ---------------------
    extUdp.beginPacket("255.255.255.255", SMBUS_EXT_PORT);
    extUdp.write((const uint8_t*)&packet, sizeof(packet));
    extUdp.endPacket();

    // -------- Debug --------------------------------
    Serial.printf("[SMBusExt] EXT: Tray=%d AV=0x%02X PIC=0x%02X SMCverRaw=%s Enc=0x%02X -> xboxVer=%d Res=%dx%d\n",
        packet.trayState,
        packet.avPackState & 0xFF,
        packet.picVer & 0xFF,
        (err == 0) ? (String("0x") + String(b, HEX)).c_str() : "ERR",
        (packet.encoderType < 0 ? 0xFF : packet.encoderType) & 0xFF,
        packet.xboxVer,
        packet.videoWidth, packet.videoHeight);
}

void SMBusExt::sendCustomStatus(const Status& status) {
    extUdp.beginPacket("255.255.255.255", SMBUS_EXT_PORT);
    extUdp.write((const uint8_t*)&status, sizeof(status));
    extUdp.endPacket();

    Serial.printf("[SMBusExt] CUSTOM EXT: Tray=%d AV=%d Ver=%d XboxVer=%d Enc=0x%02X Res=%dx%d\n",
        status.trayState, status.avPackState, status.picVer,
        status.xboxVer, status.encoderType, status.videoWidth, status.videoHeight);
}
