#include <SPI.h>
#include <LovyanGFX.hpp>
#include <AnimatedGIF.h>
#include <FFat.h>
#include "disp_cfg.h"

extern LGFX tft;

// ---- RAM GIF Structures ----
struct RAMGIFHandle {
    uint8_t *data;
    size_t size;
    size_t pos;
};
uint8_t *gifBuffer = nullptr;
size_t gifSize = 0;
AnimatedGIF gif;

// --- RAM-based GIF helpers ---
void *GIFOpenRAM(const char *, int32_t *pSize) {
    RAMGIFHandle *h = new RAMGIFHandle{gifBuffer, gifSize, 0};
    *pSize = gifSize;
    return h;
}
void GIFCloseRAM(void *handle) {
    delete static_cast<RAMGIFHandle*>(handle);
}
int32_t GIFReadRAM(GIFFILE *pFile, uint8_t *pBuf, int32_t iLen) {
    RAMGIFHandle *h = static_cast<RAMGIFHandle*>(pFile->fHandle);
    int32_t avail = h->size - h->pos;
    int32_t n = (iLen < avail) ? iLen : avail;
    if (n > 0) {
        memcpy(pBuf, h->data + h->pos, n);
        h->pos += n;
        pFile->iPos = h->pos;
    }
    return n;
}
int32_t GIFSeekRAM(GIFFILE *pFile, int32_t iPosition) {
    RAMGIFHandle *h = static_cast<RAMGIFHandle*>(pFile->fHandle);
    if (iPosition >= 0 && (size_t)iPosition < h->size) {
        h->pos = iPosition;
        pFile->iPos = iPosition;
        return iPosition;
    }
    return -1;
}

// --- GIF Draw Callback ---
void GIFDraw(GIFDRAW *pDraw) {
    if (!pDraw->pPixels || !pDraw->pPalette) return;
    int16_t y = pDraw->iY + pDraw->y;
    if (y < 0 || y >= tft.height() || pDraw->iX >= tft.width() || pDraw->iWidth < 1) return;

    int x_offset = (tft.width() - pDraw->iWidth) / 2;
    int y_offset = (tft.height() - pDraw->iHeight) / 2;

    static uint16_t lineBuffer[320]; // adjust if your GIFs are wider
    for (int x = 0; x < pDraw->iWidth; x++) {
        lineBuffer[x] = pDraw->pPalette[pDraw->pPixels[x]];
    }
    tft.pushImage(x_offset + pDraw->iX, y_offset + y, pDraw->iWidth, 1, lineBuffer);
}

void bootShowScreen() {
    tft.fillScreen(TFT_BLACK);

    // --- Prefer GIF ---
    if (FFat.exists("/boot/boot.gif")) {
        File f = FFat.open("/boot/boot.gif", "r");
        if (f && f.size() > 0) {
            gifSize = f.size();
            gifBuffer = (uint8_t*)heap_caps_malloc(gifSize, MALLOC_CAP_SPIRAM);
            if (gifBuffer) {
                f.read(gifBuffer, gifSize);
                f.close();
                Serial.printf("[Type D] Loaded boot.gif into PSRAM (%u bytes)\n", (unsigned)gifSize);

                gif.begin(GIF_PALETTE_RGB565_BE);
                if (gif.open("", GIFOpenRAM, GIFCloseRAM, GIFReadRAM, GIFSeekRAM, GIFDraw)) {
                    int startLoop = gif.getLoopCount();
                    int frameDelay = 0;
                    while (gif.playFrame(true, &frameDelay)) {
                        delay(frameDelay);
                        yield();
                        if (gif.getLoopCount() > startLoop) break;
                    }
                    gif.close();
                    Serial.println("[Type D] GIF playback finished");
                } else {
                    Serial.println("[Type D] Failed to open GIF from RAM");
                }
                heap_caps_free(gifBuffer); gifBuffer = nullptr;
                return;
            } else {
                Serial.println("[Type D] PSRAM alloc failed!");
                f.close();
            }
        } else {
            Serial.println("[Type D] /boot/boot.gif not found or size==0, skipping animation.");
        }
    }

    // --- Next: JPG using LovyanGFX-native decoder (buffered, top-left only) ---
    if (FFat.exists("/boot/boot.jpg")) {
        File jpgFile = FFat.open("/boot/boot.jpg", "r");
        if (jpgFile && jpgFile.size() > 0) {
            size_t jpgSize = jpgFile.size();
            uint8_t* jpgBuffer = (uint8_t*)heap_caps_malloc(jpgSize, MALLOC_CAP_SPIRAM);
            if (jpgBuffer) {
                jpgFile.read(jpgBuffer, jpgSize);
                jpgFile.close();
                tft.drawJpg(jpgBuffer, jpgSize, 0, 0);
                heap_caps_free(jpgBuffer);
                delay(1200);
                return;
            } else {
                jpgFile.close();
                Serial.println("[Type D] PSRAM alloc failed!");
            }
        } else {
            Serial.println("[Type D] Could not open or determine boot.jpg size.");
        }
    }

    // --- Fallback: Splash Text ---
    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(middle_center);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setTextSize(3);
    tft.drawString("Type D", tft.width() / 2, tft.height() / 2 - 18);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(1);
    tft.drawString(VERSION_TEXT, tft.width() / 2, tft.height() / 2 + 10);
    delay(1500);
}
