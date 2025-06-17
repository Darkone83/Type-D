#pragma once

#include <Arduino.h>
#include <vector>

class LGFX;

namespace ImageDisplay {

    bool isDone();
    
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

} // namespace ImageDisplay
