#pragma once

#include <Arduino.h>
#include <vector>

class LGFX;

namespace ImageDisplay {
    
    extern bool paused;
    void setPaused(bool p);

enum Mode {
    MODE_RANDOM,
    MODE_JPG,
    MODE_GIF
};

void begin(LGFX* tft);

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
void update();
void clear();
void showIdle();

const std::vector<String>& getJpgList();
const std::vector<String>& getGifList();

// These are now file-static, not global; you do NOT need to use them directly outside the implementation
// void* GIFOpenRAM(const char*, int32_t*, void* userData);
// void GIFCloseRAM(void* handle);
// int32_t GIFReadRAM(GIFFILE* pFile, uint8_t* pBuf, int32_t iLen);
// int32_t GIFSeekRAM(GIFFILE* pFile, int32_t iPosition);

} // namespace ImageDisplay
