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
    RandomGIF
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

}
