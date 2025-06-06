#pragma once

#include <TFT_eSPI.h>
#include <vector>
#include <string>

namespace Gallery {

// Gallery states
enum class Mode {
    None,
    BrowseJPG,
    BrowseGIF,
    RandomJPG,
    RandomGIF,
    MODE_JPG = 1,
    MODE_GIF = 2
};

// Initialize gallery module with TFT instance
void begin(TFT_eSPI* tft);

// Open gallery submenu with selected mode
void open(Mode mode);

// Close gallery submenu, return control to UI
void close();

// Process touch input: swipes, taps, drag for scrolling
void handleGesture(uint8_t gesture, int16_t x = 0, int16_t y = 0);

// Draw current gallery menu or image
void draw();

// Update gallery state (for animations, random cycling)
void update();

// --- Minimal required API for command codes ---
// These must exist so that code like Gallery::nextImage() compiles:
void nextImage();
void prevImage();
void startRandomMode(Mode mode);

} // namespace Gallery
