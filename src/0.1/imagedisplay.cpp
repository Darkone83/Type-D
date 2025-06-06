#include "imagedisplay.h"
#include <vector>
#include <algorithm>
#include <TJpg_Decoder.h>
#include <AnimatedGIF.h>
#include <SD_MMC.h>

using namespace ImageDisplay;

static TFT_eSPI* _tft = nullptr;
static AnimatedGIF gif;
static Mode currentMode = MODE_RANDOM;
static std::vector<String> jpgList;
static std::vector<String> gifList;
static std::vector<String> randomStack;
static int imgIndex = 0;

// --- PATCH: Auto-advance state ---
static unsigned long lastImageChange = 0;
static bool currentIsGif = false;

bool ImageDisplay::gifBootActive = false; // PATCHED: Implement gifBootActive as requested

void ImageDisplay::begin(TFT_eSPI* tft) {
    _tft = tft;
    refreshFileLists();
    currentMode = MODE_RANDOM;
    gifBootActive = false;
}

void ImageDisplay::setMode(Mode m) {
    currentMode = m;
    imgIndex = 0;
}

Mode ImageDisplay::getMode() {
    return currentMode;
}

void ImageDisplay::refreshFileLists() {
    jpgList.clear();
    gifList.clear();

    File jpgDir = SD_MMC.open("/jpg");
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

    File gifDir = SD_MMC.open("/gif");
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

void ImageDisplay::displayImage(const String& path) {
    _tft->fillScreen(TFT_BLACK);
    String lower = path;
    lower.toLowerCase();
    if (lower.endsWith(".jpg") || lower.endsWith(".jpeg")) {
        TJpgDec.drawSdJpg(0, 0, path.c_str());
        lastImageChange = millis();
        currentIsGif = false;
        gifBootActive = false;
    } else if (lower.endsWith(".gif")) {
        gif.open(path.c_str(), GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, gifDraw);
        lastImageChange = millis();
        currentIsGif = true;
        gifBootActive = false;
    }
}

void ImageDisplay::displayRandomImage() {
    refreshFileLists();
    randomStack.clear();
    for (auto& f : jpgList) randomStack.push_back(f);
    for (auto& f : gifList) randomStack.push_back(f);
    if (randomStack.empty()) return;
    std::random_shuffle(randomStack.begin(), randomStack.end());
    imgIndex = 0;
    displayImage(randomStack[imgIndex]);
}

void ImageDisplay::displayRandomJpg() {
    refreshFileLists();
    if (jpgList.empty()) return;
    std::random_shuffle(jpgList.begin(), jpgList.end());
    imgIndex = 0;
    setMode(MODE_JPG);
    displayImage(jpgList[imgIndex]);
}

void ImageDisplay::displayRandomGif() {
    refreshFileLists();
    if (gifList.empty()) return;
    std::random_shuffle(gifList.begin(), gifList.end());
    imgIndex = 0;
    setMode(MODE_GIF);
    displayImage(gifList[imgIndex]);
}

void ImageDisplay::nextImage() {
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

void ImageDisplay::prevImage() {
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

void ImageDisplay::loop() {
    // No changes for this request; legacy compatibility
}

// --- PATCH: Auto-advance update() function ---
void ImageDisplay::update() {
    if (currentMode != MODE_RANDOM) return;
    if (!currentIsGif) {
        if (millis() - lastImageChange > 2000) {
            imgIndex = (imgIndex + 1) % randomStack.size();
            displayImage(randomStack[imgIndex]);
        }
    } else {
        // Play a GIF frame; if 0 returned, GIF has ended
        int ret = gif.playFrame(false, nullptr);
        if (ret == 0) { // GIF finished, advance
            imgIndex = (imgIndex + 1) % randomStack.size();
            displayImage(randomStack[imgIndex]);
        }
    }
}

// --- GIF helpers (unchanged) ---
void* ImageDisplay::GIFOpenFile(const char* fname, int32_t* pSize) {
    File* pFile = new File(SD_MMC.open(fname));
    if (!(*pFile)) {
        delete pFile;
        return NULL;
    }
    *pSize = (int32_t)pFile->size();
    return (void*)pFile;
}

void ImageDisplay::GIFCloseFile(void* handle) {
    File* pFile = static_cast<File*>(handle);
    if (pFile) pFile->close();
    delete pFile;
}

int32_t ImageDisplay::GIFReadFile(GIFFILE* pFile, uint8_t* pBuf, int32_t iLen) {
    File* f = (File*)(pFile->fHandle);
    if (!f) return 0;
    return f->read(pBuf, iLen);
}

int32_t ImageDisplay::GIFSeekFile(GIFFILE* pFile, int32_t iPosition) {
    File* f = (File*)(pFile->fHandle);
    if (!f) return 0;
    f->seek(iPosition);
    return iPosition;
}

// --- PATCH: Correct GIF draw function for AnimatedGIF ---
void ImageDisplay::gifDraw(GIFDRAW* pDraw) {
    if (!_tft || !pDraw->pPalette) return;
    if (pDraw->y >= _tft->height()) return;
    for (int x = 0; x < pDraw->iWidth; x++) {
        uint8_t idx = pDraw->pPixels[x];
        uint16_t color = pDraw->pPalette[idx];
        _tft->drawPixel(pDraw->iX + x, pDraw->iY, color);
    }
}

void ImageDisplay::showIdle() {
    // ... optional: draw idle state ...
}

void ImageDisplay::clear() {
    if (_tft) _tft->fillScreen(TFT_BLACK);
}
