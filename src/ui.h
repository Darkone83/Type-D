#ifndef UI_H
#define UI_H

#include <TFT_eSPI.h>

namespace UI {

// Call once at startup (pass TFT instance if needed)
void begin(TFT_eSPI* tft);

// Show the main menu overlay (Settings, Gallery, About)
void showMenu();

// Call this from your loop; handles menu logic if menu is active
void loop(uint8_t gesture);

// Returns true if the menu is currently visible
bool menuVisible();

// Returns true if menu should exit (double tap detected)
bool shouldExitMenu();

// Call this after handling menu exit to reset the flag
void resetMenuExit();

// SETTINGS SUBMENU (Brightness)

// Call to show the brightness submenu
void showBrightnessMenu();

// Call from your loop to handle brightness slider/tap interaction
// Provide raw touch X/Y coordinates and tap event (true if tap)
void brightnessMenuLoop(int16_t x, int16_t y, bool tap);

// Returns true if the brightness menu is active
bool brightnessMenuActive();

// Returns the current brightness percentage (5 to 100)
int getBrightness();

// Set brightness (for boot restore, etc)
void setBrightness(int percent);

// Ensure these are present for the command API
void restartWiFiManager();
void showAbout();

void redrawActive();

void drawBrightnessMenu();

} // namespace UI

#endif
