#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <SD_MMC.h>
#include <TJpg_Decoder.h>
#include <AnimatedGIF.h>

namespace ImageDisplay {

    enum Mode {
        MODE_RANDOM,
        MODE_JPG,
        MODE_GIF
    };

    extern bool gifBootActive;

    void begin(TFT_eSPI* tft);
    void setMode(Mode m);
    Mode getMode();
    void refreshFileLists();
    void displayImage(const String& path);
    void displayRandomImage();
    void displayRandomJpg();
    void displayRandomGif();
    void nextImage();
    void prevImage();
    void loop();
    void showIdle();
    void clear();
    void gifDraw(GIFDRAW* pDraw);
    void* GIFOpenFile(const char* fname, int32_t* pSize);
    void GIFCloseFile(void* handle);
    int32_t GIFReadFile(GIFFILE* pFile, uint8_t* pBuf, int32_t iLen);
    int32_t GIFSeekFile(GIFFILE* pFile, int32_t iPosition);

    // --- PATCH: Add update() declaration for auto-advance logic in random mode
    void update();

} // namespace ImageDisplay
