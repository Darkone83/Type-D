#include <SPI.h>
#include "imagedisplay.h"
#include <vector>
#include <algorithm>
#include <AnimatedGIF.h>
#include <FFat.h>
#include <LovyanGFX.hpp>
#include "esp_heap_caps.h"
#include "disp_cfg.h"

class LGFX;

namespace ImageDisplay {

bool paused = false;

void setPaused(bool p) { paused = p; }

// --- File lists and display mode state ---
static LGFX* _tft = nullptr;
static AnimatedGIF gif;
static Mode currentMode = MODE_RANDOM;
static std::vector<String> jpgList;
static std::vector<String> gifList;
static std::vector<String> randomStack;
static int imgIndex = 0;
static unsigned long lastImageChange = 0;
static bool currentIsGif = false;

// --- RAMGIFHandle for GIF-in-RAM logic ---
struct RAMGIFHandle {
    uint8_t *data;
    size_t size;
    size_t pos;
};
static RAMGIFHandle* s_gifHandle = nullptr;

// --- Utility: Always free GIF RAM handle/buffer safely ---
static void freeRamGifHandle() {
    if (s_gifHandle) {
        if (s_gifHandle->data) {
            heap_caps_free(s_gifHandle->data);
            s_gifHandle->data = nullptr;
        }
        delete s_gifHandle;
        s_gifHandle = nullptr;
    }
    currentIsGif = false; // <-- Always mark as not in GIF playback after freeing
}

// --- GIF RAM Callbacks (Larry Bank style) ---
void* GIFOpenRAM(const char*, int32_t* pSize) {
    if (!s_gifHandle) return nullptr;
    *pSize = s_gifHandle->size;
    return s_gifHandle;
}
void GIFCloseRAM(void* hptr) {
    // No op. We free after playback (or on error).
}
int32_t GIFReadRAM(GIFFILE* pFile, uint8_t* pBuf, int32_t iLen) {
    RAMGIFHandle* h = static_cast<RAMGIFHandle*>(pFile->fHandle);
    int32_t avail = h->size - h->pos;
    int32_t n = (iLen < avail) ? iLen : avail;
    if (n > 0) {
        memcpy(pBuf, h->data + h->pos, n);
        h->pos += n;
        pFile->iPos = h->pos;
    }
    return n;
}
int32_t GIFSeekRAM(GIFFILE* pFile, int32_t iPosition) {
    RAMGIFHandle* h = static_cast<RAMGIFHandle*>(pFile->fHandle);
    if (iPosition >= 0 && (size_t)iPosition < h->size) {
        h->pos = iPosition;
        pFile->iPos = iPosition;
        return iPosition;
    }
    return -1;
}

// --- GIF draw callback ---
void gifDraw(GIFDRAW* pDraw) {
    if (!_tft || !pDraw || !pDraw->pPalette || !pDraw->pPixels) return;
    int x_offset = (_tft->width() - pDraw->iWidth) / 2;
    int y_offset = (_tft->height() - pDraw->iHeight) / 2;
    int16_t y = pDraw->iY + pDraw->y;
    if (y < 0 || y >= _tft->height() || pDraw->iX >= _tft->width() || pDraw->iWidth < 1) return;
    static uint16_t lineBuffer[320];
    for (int x = 0; x < pDraw->iWidth; x++) {
        lineBuffer[x] = pDraw->pPalette[pDraw->pPixels[x]];
    }
    _tft->pushImage(x_offset + pDraw->iX, y_offset + y, pDraw->iWidth, 1, lineBuffer);
}

// Defensive: Only close GIF, never free buffer here
void closeGif() {
    gif.close();
    // Don't free RAM here, free after playback!
}

// --- API Implementation ---

void begin(LGFX* tft) {
    _tft = tft;
    refreshFileLists();
    currentMode = MODE_RANDOM;
}

void setMode(Mode m) {
    currentMode = m;
    imgIndex = 0;
}

Mode getMode() {
    return currentMode;
}

void refreshFileLists() {
    jpgList.clear();
    gifList.clear();

    File jpgDir = FFat.open("/jpg");
    if (jpgDir && jpgDir.isDirectory()) {
        File f = jpgDir.openNextFile();
        while (f) {
            if (!f.isDirectory()) {
                String fn = String(f.name());
                fn.toLowerCase();
                if (fn.endsWith(".jpg") || fn.endsWith(".jpeg")) {
                    jpgList.push_back(String("/jpg/") + String(f.name()));
                }
            }
            f = jpgDir.openNextFile();
        }
    }

    File gifDir = FFat.open("/gif");
    if (gifDir && gifDir.isDirectory()) {
        File f = gifDir.openNextFile();
        while (f) {
            if (!f.isDirectory()) {
                String fn = String(f.name());
                fn.toLowerCase();
                if (fn.endsWith(".gif")) {
                    gifList.push_back(String("/gif/") + String(f.name()));
                }
            }
            f = gifDir.openNextFile();
        }
    }
}

void displayImage(const String& path) {
    if (!_tft) {
        Serial.println("[ImageDisplay] _tft pointer is NULL!");
        return;
    }
    _tft->fillScreen(TFT_BLACK);

    closeGif();           // Defensive: always close old GIF
    freeRamGifHandle();   // Always free previous RAM buffer

    currentIsGif = false; // Reset before detection

    String lower = path;
    lower.toLowerCase();

    if (lower.endsWith(".jpg") || lower.endsWith(".jpeg")) {
        File jpgFile = FFat.open(path, "r");
        if (jpgFile && jpgFile.size() > 0) {
            size_t jpgSize = jpgFile.size();
            uint8_t* jpgBuffer = (uint8_t*)heap_caps_malloc(jpgSize, MALLOC_CAP_SPIRAM);
            if (jpgBuffer) {
                int bytesRead = jpgFile.read(jpgBuffer, jpgSize);
                jpgFile.close();
                if ((size_t)bytesRead != jpgSize) {
                    Serial.printf("[ImageDisplay] JPG read mismatch: %d != %u\n", bytesRead, jpgSize);
                }
                _tft->drawJpg(jpgBuffer, jpgSize, 0, 0);
                heap_caps_free(jpgBuffer);
                jpgBuffer = nullptr;
            } else {
                jpgFile.close();
                Serial.println("[ImageDisplay] PSRAM alloc failed!");
            }
        } else {
            Serial.println("[ImageDisplay] JPG file open or size failed!");
            if (jpgFile) jpgFile.close();
        }
    } else if (lower.endsWith(".gif")) {
        Serial.printf("[ImageDisplay] Loading GIF: %s\n", path.c_str());
        File f = FFat.open(path, "r");
        if (f && f.size() > 0) {
            size_t gifSize = f.size();
            uint8_t* gifBuffer = (uint8_t*)heap_caps_malloc(gifSize, MALLOC_CAP_SPIRAM);
            if (gifBuffer) {
                int bytesRead = f.read(gifBuffer, gifSize);
                f.close();
                if ((size_t)bytesRead != gifSize) {
                    Serial.printf("[ImageDisplay] GIF read mismatch: %d != %u\n", bytesRead, gifSize);
                }
                // Free any old handle/buffer
                freeRamGifHandle();
                s_gifHandle = new RAMGIFHandle{gifBuffer, gifSize, 0};
                gif.begin(GIF_PALETTE_RGB565_BE);
                if (gif.open("", GIFOpenRAM, GIFCloseRAM, GIFReadRAM, GIFSeekRAM, gifDraw)) {
                    currentIsGif = true; // Only TRUE while GIF handle is valid!
                    int startLoop = gif.getLoopCount();
                    int frameDelay = 0;
                    while (gif.playFrame(true, &frameDelay)) {
                        delay(frameDelay);
                        yield();
                        if (gif.getLoopCount() > startLoop) break;
                    }
                    gif.close();
                    Serial.println("[ImageDisplay] GIF playback finished");
                    freeRamGifHandle(); // Always free after playback
                    currentIsGif = false; // Mark as done!
                } else {
                    Serial.println("[ImageDisplay] GIF decoder failed to open RAM file!");
                    freeRamGifHandle(); // Always free after error
                    currentIsGif = false;
                }
            } else {
                f.close();
                Serial.println("[ImageDisplay] GIF PSRAM alloc failed!");
                currentIsGif = false;
            }
        } else {
            Serial.println("[ImageDisplay] GIF file open or size failed!");
            if (f) f.close();
            currentIsGif = false;
        }
    }
    lastImageChange = millis();
}

void displayRandomImage() {
    refreshFileLists();
    randomStack.clear();
    for (auto& f : jpgList) randomStack.push_back(f);
    for (auto& f : gifList) randomStack.push_back(f);
    if (randomStack.empty()) {
        Serial.println("[ImageDisplay] No images to display.");
        return;
    }
    std::random_shuffle(randomStack.begin(), randomStack.end());
    imgIndex = 0;
    displayImage(randomStack[imgIndex]);
}

void displayRandomJpg() {
    refreshFileLists();
    if (jpgList.empty()) return;
    std::random_shuffle(jpgList.begin(), jpgList.end());
    imgIndex = 0;
    setMode(MODE_JPG);
    displayImage(jpgList[imgIndex]);
}

void displayRandomGif() {
    refreshFileLists();
    if (gifList.empty()) return;
    std::random_shuffle(gifList.begin(), gifList.end());
    imgIndex = 0;
    setMode(MODE_GIF);
    displayImage(gifList[imgIndex]);
}

void nextImage() {
    if (currentMode == MODE_RANDOM && !randomStack.empty()) {
        imgIndex = (imgIndex + 1) % randomStack.size();
        displayImage(randomStack[imgIndex]);
    } else if (currentMode == MODE_JPG && !jpgList.empty()) {
        imgIndex = (imgIndex + 1) % jpgList.size();
        displayImage(jpgList[imgIndex]);
    } else if (currentMode == MODE_GIF && !gifList.empty()) {
        imgIndex = (imgIndex + 1) % gifList.size();
        displayImage(gifList[imgIndex]);
    }
}

void prevImage() {
    if (currentMode == MODE_RANDOM && !randomStack.empty()) {
        imgIndex = (imgIndex - 1 + randomStack.size()) % randomStack.size();
        displayImage(randomStack[imgIndex]);
    } else if (currentMode == MODE_JPG && !jpgList.empty()) {
        imgIndex = (imgIndex - 1 + jpgList.size()) % jpgList.size();
        displayImage(jpgList[imgIndex]);
    } else if (currentMode == MODE_GIF && !gifList.empty()) {
        imgIndex = (imgIndex - 1 + gifList.size()) % gifList.size();
        displayImage(gifList[imgIndex]);
    }
}

void loop() {
    // Legacy compatibility; no changes
}

void update() {
    if (paused) return; 
    
    if (currentMode != MODE_RANDOM) return;
    if (!currentIsGif) {
        if (millis() - lastImageChange > 2000) {
            imgIndex = (imgIndex + 1) % randomStack.size();
            displayImage(randomStack[imgIndex]);
        }
    } else {
        // Only called while a GIF is truly active!
        int ret = gif.playFrame(false, nullptr);
        if (ret == 0) {
            imgIndex = (imgIndex + 1) % randomStack.size();
            displayImage(randomStack[imgIndex]);
        }
    }
}

void showIdle() {
    // Optional: draw idle state
}

void clear() {
    if (_tft) _tft->fillScreen(TFT_BLACK);
}

const std::vector<String>& getJpgList() { return jpgList; }
const std::vector<String>& getGifList() { return gifList; }

} // namespace ImageDisplay
