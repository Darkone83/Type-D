#include <FS.h>
using namespace fs;
#include <WebServer.h>
#include "ui.h"
#include <Preferences.h>
#include "disp_cfg.h"
#include <Arduino.h>       // For analogWrite, pinMode, delay
#include <WiFiManager.h>   // For wm access
#include <SD_MMC.h>        // For SD card image access
#include "imagedisplay.h"  // Your image display helper functions
#include "disp_cfg.h"  // For VERSION_TEXT
#include <TJpg_Decoder.h>  // For TJpgDec
#include "imagedisplay.h"  // Your image display namespace (if exists)
// --- PATCH: Add Gallery include ---
#include "gallery.h"

namespace ImageDisplay {

void displayImageCentered(const char* path, int centerX, int centerY) {
    uint16_t w = 0, h = 0;
    if (TJpgDec.getJpgSize(&w, &h, path) == 0) {
        int x = centerX - w / 2;
        int y = centerY - h / 2;
        TJpgDec.drawSdJpg(x, y, path);
    }
}

} // namespace ImageDisplay

extern WiFiManager wm;             // WiFiManager instance from your .ino
extern void restartWiFiManager(); // Function to restart WiFiManager portal

static TFT_eSPI* _tft = nullptr;

// --- UI States ---
enum UiMenuState {
    MAIN_MENU,
    SETTINGS_MENU,
    BRIGHTNESS_MENU,
    ABOUT_MENU,
    // --- PATCH: Gallery state
    GALLERY_MENU
};

static UiMenuState currentMenu = MAIN_MENU;

// --- Menu Selections ---
static int mainSelectedIndex = 0;
static int settingsSelectedIndex = 0;

// --- Brightness State ---
static int _brightness = 100; // percent
static const int _minBrightness = 5;
static const int _maxBrightness = 100;
static const int _brightnessStep = 5;

static Preferences _prefs;

// Backlight pin (low side switch on GPIO32)
#define TFT_BL_PIN 32

// Slider geometry (240x240 display)
static const int sliderY = 90;
static const int sliderX0 = 40;
static const int sliderX1 = 200;
static const int sliderW = sliderX1 - sliderX0;
static const int sliderH = 18;

// --- Menu Items ---

// Main Menu Items (4 items including About)
static const char* mainMenuItems[] = {
    "Settings",       // [0] Main Menu - Settings
    "Gallery",        // [1] Main Menu - Gallery
    "About",          // [2] Main Menu - About
    // Add more if needed
};
static const int mainMenuCount = sizeof(mainMenuItems) / sizeof(mainMenuItems[0]);

// Settings Menu Items (4 items)
static const char* settingsMenuItems[] = {
    "Brightness",         // [0] Settings Menu - Brightness submenu
    "Restart WiFi Portal",// [1] Settings Menu - Restart WiFiManager portal
    "Forget WiFi Network",// [2] Settings Menu - Forget saved WiFi
    "Back"                // [3] Settings Menu - Back to Main Menu
};
static const int settingsMenuCount = sizeof(settingsMenuItems) / sizeof(settingsMenuItems[0]);

namespace UI {

// -- PWM backlight setup and control (low-side on GPIO32) --
static void setupBacklightPWM() {
    pinMode(TFT_BL_PIN, OUTPUT);
}

static void setBacklightPWM(int brightnessPercent) {
    uint8_t duty = 255 - map(brightnessPercent, _minBrightness, _maxBrightness, 0, 255);
    analogWrite(TFT_BL_PIN, duty);
}

// Initialize UI and brightness from preferences
void begin(TFT_eSPI* tft) {
    _tft = tft;
    _prefs.begin("typed", false);
    _brightness = _prefs.getInt("brightness", 100);
    setupBacklightPWM();
    setBacklightPWM(_brightness);
    currentMenu = MAIN_MENU;
    mainSelectedIndex = 0;
    settingsSelectedIndex = 0;
}

// --- Drawing functions ---

// Draw Main Menu
void drawMainMenu() {
    _tft->fillScreen(_tft->color565(16, 124, 16)); // Xbox green background
    _tft->setTextFont(4);
    _tft->setTextColor(TFT_WHITE);
    _tft->setTextDatum(MC_DATUM);
    _tft->drawString("Main Menu", _tft->width() / 2, 30);

    for (int i = 0; i < mainMenuCount; i++) {
        int y = 70 + i * 45;
        if (i == mainSelectedIndex) {
            _tft->fillRoundRect(30, y - 18, _tft->width() - 60, 36, 12, TFT_WHITE);
            _tft->setTextColor(_tft->color565(16, 124, 16));
        } else {
            _tft->setTextColor(TFT_WHITE);
        }
        _tft->drawString(mainMenuItems[i], _tft->width() / 2, y);
    }

    _tft->setTextFont(2);
    _tft->setTextColor(TFT_WHITE);
    _tft->drawString("Double tap to exit menu", _tft->width() / 2, _tft->height() - 20);
}

// Draw Settings Menu
void drawSettingsMenu() {
    _tft->fillScreen(_tft->color565(16, 124, 16)); // Xbox green background
    _tft->setTextFont(4);
    _tft->setTextColor(TFT_WHITE);
    _tft->setTextDatum(MC_DATUM);
    _tft->drawString("Settings", _tft->width() / 2, 30);

    _tft->setTextFont(2);
    for (int i = 0; i < settingsMenuCount; i++) {
        int y = 70 + i * 35;
        if (i == settingsSelectedIndex) {
            _tft->fillRoundRect(10, y - 16, _tft->width() - 20, 32, 6, TFT_WHITE);
            _tft->setTextColor(_tft->color565(16, 124, 16));
        } else {
            _tft->setTextColor(TFT_WHITE);
        }
        _tft->drawString(settingsMenuItems[i], _tft->width() / 2, y);
    }
}

// Draw Brightness Slider Menu
static void drawBrightnessMenu() {
    _tft->fillScreen(_tft->color565(16,124,16)); // Xbox green background
    _tft->setTextDatum(MC_DATUM);
    _tft->setTextFont(4);
    _tft->setTextColor(TFT_WHITE);
    _tft->drawString("Brightness", _tft->width()/2, 35);

    // Slider background bar
    _tft->fillRoundRect(sliderX0, sliderY, sliderW, sliderH, 8, TFT_DARKGREY);

    // Slider fill portion
    int fillW = (_brightness - _minBrightness) * (sliderW - 4) / (_maxBrightness - _minBrightness);
    if (fillW < 4) fillW = 4;
    _tft->fillRoundRect(sliderX0 + 2, sliderY + 2, fillW, sliderH - 4, 6, TFT_GREEN);

    // Draw "-" and "+" symbols
    _tft->setTextFont(4);
    _tft->setTextColor(TFT_WHITE);
    _tft->drawString("-", sliderX0 - 22, sliderY + sliderH / 2);
    _tft->drawString("+", sliderX1 + 18, sliderY + sliderH / 2);

    // Percentage text below slider
    _tft->setTextFont(2);
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", _brightness);
    _tft->drawString(buf, _tft->width() / 2, sliderY + sliderH + 20);
}

// --- Animated grow animation for Settings menu ---
static void drawSettingsMenuScaled(float scale) {
    _tft->fillScreen(_tft->color565(16, 124, 16)); // Xbox green background

    int centerX = _tft->width() / 2;
    int baseY = 30;

    int fontSize = 2 + (int)(scale * 2); // scale font size between 2 and 4
    if (fontSize > 4) fontSize = 4;

    int titleY = (int)(baseY * scale + (_tft->height() / 2) * (1 - scale));

    _tft->setTextDatum(MC_DATUM);
    _tft->setTextFont(fontSize);
    _tft->setTextColor(TFT_WHITE);
    _tft->drawString("Settings", centerX, titleY);

    _tft->setTextFont(2);
    for (int i = 0; i < settingsMenuCount; i++) {
        int y = (int)((70 + i * 35) * scale + (_tft->height() / 2) * (1 - scale));
        if (i == settingsSelectedIndex) {
            int rectWidth = (int)(_tft->width() * scale) - 20;
            int rectX = (int)(10 * scale);
            int rectHeight = (int)(32 * scale);
            _tft->fillRoundRect(rectX, y - rectHeight / 2, rectWidth, rectHeight, (int)(6 * scale), TFT_WHITE);
            _tft->setTextColor(_tft->color565(16, 124, 16));
        } else {
            _tft->setTextColor(TFT_WHITE);
        }
        _tft->drawString(settingsMenuItems[i], centerX, y);
    }
}

// --- Fade overlay helper ---
static void fadeOverlay(uint8_t startAlpha, uint8_t endAlpha, uint8_t step = 15, uint16_t delayMs = 20) {
    if (!_tft) return;
    int direction = (endAlpha > startAlpha) ? 1 : -1;
    for (int alpha = startAlpha; alpha != endAlpha; alpha += direction * step) {
        if (alpha < 0) alpha = 0;
        if (alpha > 255) alpha = 255;
        // Simple fill black overlay for fade effect
        _tft->fillRect(0, 0, _tft->width(), _tft->height(), _tft->color565(0, 0, 0));
        delay(delayMs);
    }
}

// --- About menu implementation ---
void showAbout() {
    if (!_tft) return;

    // Fade out any current content fully
    fadeOverlay(0, 255);

    auto drawCenteredTextLines = [&](const char** lines, int count) {
        int lineHeight = 30;
        int totalHeight = lineHeight * count;
        int startY = (_tft->height() - totalHeight) / 2;
        _tft->fillScreen(TFT_BLACK);
        _tft->setTextDatum(MC_DATUM);
        _tft->setTextColor(TFT_WHITE);

        for (int i = 0; i < count; i++) {
            _tft->setTextFont(4);
            _tft->drawString(lines[i], _tft->width() / 2, startY + i * lineHeight);
        }
    };

    // 1) "Type D" + version line
    const char* typeDLines[] = {
        "Type D",
        VERSION_TEXT
    };
    drawCenteredTextLines(typeDLines, 2);
    fadeOverlay(255, 0);
    delay(1500);
    fadeOverlay(0, 255);

    // 2) "Concept by: Andr0"
    const char* conceptLines[] = { "Concept by: Andr0" };
    drawCenteredTextLines(conceptLines, 1);
    fadeOverlay(255, 0);
    delay(1500);
    fadeOverlay(0, 255);

    // 3) "Code By: Darkone83" + DC.jpg
    _tft->fillScreen(TFT_BLACK);
    _tft->setTextDatum(MC_DATUM);
    _tft->setTextFont(4);
    _tft->setTextColor(TFT_WHITE);
    _tft->drawString("Code By: Darkone83", _tft->width() / 2, 40);

    if (SD_MMC.exists("/resources/DC.jpg")) {
        ImageDisplay::displayImageCentered("/resources/DC.jpg", _tft->width() / 2, 140);
    }
    fadeOverlay(255, 0);
    delay(1500);
    fadeOverlay(0, 255);

    // 4) Show TR.jpg full screen
    if (SD_MMC.exists("/resources/TR.jpg")) {
        ImageDisplay::displayImageCentered("/resources/TR.jpg", _tft->width() / 2, _tft->height() / 2);
    }
    fadeOverlay(255, 0);
    delay(1500);
    fadeOverlay(0, 255);

    // 5) Show XBS.jpg full screen
    if (SD_MMC.exists("/resources/XBS.jpg")) {
        ImageDisplay::displayImageCentered("/resources/XBS.jpg", _tft->width() / 2, _tft->height() / 2);
    }
    fadeOverlay(255, 0);
    delay(1500);
    fadeOverlay(0, 255);

    // Return to Main Menu
    currentMenu = MAIN_MENU;
    showMenu();
}

// --- Show menus ---
void showMenu() {
    switch (currentMenu) {
        case MAIN_MENU:
            drawMainMenu();
            break;
        case SETTINGS_MENU:
            drawSettingsMenu();
            break;
        case BRIGHTNESS_MENU:
            drawBrightnessMenu();
            break;
        case ABOUT_MENU:
            showAbout();
            break;
        // --- PATCH: Show Gallery menu
        case GALLERY_MENU:
            Gallery::draw();
            break;
    }
}

// --- Menu visibility & exit ---
static bool _menuActive = false;
static bool _menuShouldExit = false;

bool menuVisible() {
    return _menuActive;
}

void resetMenuExit() {
    _menuShouldExit = false;
}

bool shouldExitMenu() {
    return _menuShouldExit;
}

// --- Show and manage menu lifecycle ---
void openMenu() {
    _menuActive = true;
    _menuShouldExit = false;
    currentMenu = MAIN_MENU;
    mainSelectedIndex = 0;
    settingsSelectedIndex = 0;
    showMenu();
}

void closeMenu() {
    _menuActive = false;
    _menuShouldExit = true;
}

// --- Menu interaction ---

void loop(uint8_t gesture) {
    if (!_menuActive) return;

    switch (currentMenu) {
        case MAIN_MENU:
            if (gesture == 1) { // Up
                if (mainSelectedIndex > 0) mainSelectedIndex--;
                showMenu();
            } else if (gesture == 2) { // Down
                if (mainSelectedIndex < mainMenuCount - 1) mainSelectedIndex++;
                showMenu();
            } else if (gesture == 6) { // Double tap - exit menu
                closeMenu();
            } else if (gesture == 5) { // Single tap - select
                switch (mainSelectedIndex) {
                    case 0: // Settings
                        // Animate grow
                        for (float scale = 0.1f; scale <= 1.0f; scale += 0.1f) {
                            drawSettingsMenuScaled(scale);
                            delay(30);
                        }
                        currentMenu = SETTINGS_MENU;
                        settingsSelectedIndex = 0;
                        showMenu();
                        break;
                    case 1: // Gallery
                        // --- PATCH: Call Gallery open and switch menu state ---
                        Gallery::open(Gallery::Mode::None); // You may set mode as needed
                        currentMenu = GALLERY_MENU;
                        showMenu();
                        break;
                    case 2: // About
                        currentMenu = ABOUT_MENU;
                        showMenu();
                        break;
                }
            }
            break;

        case SETTINGS_MENU:
            if (gesture == 1) { // Up
                if (settingsSelectedIndex > 0) settingsSelectedIndex--;
                showMenu();
            } else if (gesture == 2) { // Down
                if (settingsSelectedIndex < settingsMenuCount - 1) settingsSelectedIndex++;
                showMenu();
            } else if (gesture == 6) { // Double tap - back to main menu
                currentMenu = MAIN_MENU;
                showMenu();
            } else if (gesture == 5) { // Single tap - select
                switch (settingsSelectedIndex) {
                    case 0: // Brightness submenu
                        currentMenu = BRIGHTNESS_MENU;
                        showMenu();
                        break;
                    case 1: // Restart WiFi portal
                        restartWiFiManager();
                        currentMenu = SETTINGS_MENU;
                        showMenu();
                        break;
                    case 2: // Forget WiFi network
                        wm.resetSettings();
                        restartWiFiManager();
                        currentMenu = SETTINGS_MENU;
                        showMenu();
                        break;
                    case 3: // Back to main menu
                        currentMenu = MAIN_MENU;
                        showMenu();
                        break;
                }
            }
            break;

        case BRIGHTNESS_MENU:
            // BRIGHTNESS_MENU gestures handled in brightnessMenuLoop()
            break;

        case ABOUT_MENU:
            // No gesture control during About (auto-returns to main menu)
            break;

        // --- PATCH: Gallery menu gesture handling
        case GALLERY_MENU:
            if (gesture == 6) { // Double tap to exit Gallery menu
                Gallery::close();
                currentMenu = MAIN_MENU;
                showMenu();
            } else {
                Gallery::handleGesture(gesture, 0, 0);
                Gallery::update();
                Gallery::draw();
            }
            break;
    }
}

// --- Brightness submenu interaction ---

void brightnessMenuLoop(int16_t x, int16_t y, bool tap) {
    if (currentMenu != BRIGHTNESS_MENU || !tap) return;

    // Tap anywhere on slider bar to set brightness directly
    if (x > sliderX0 && x < sliderX1 && y > sliderY - 10 && y < sliderY + sliderH + 10) {
        int sliderPos = x - sliderX0;
        int newBrightness = _minBrightness + sliderPos * (_maxBrightness - _minBrightness) / (sliderW);
        setBrightness(newBrightness);
        return;
    }
    // Tap "-" button area
    if (x > sliderX0 - 34 && x < sliderX0 - 10 && y > sliderY - 10 && y < sliderY + sliderH + 10) {
        if (_brightness > _minBrightness) setBrightness(_brightness - _brightnessStep);
        return;
    }
    // Tap "+" button area
    if (x > sliderX1 + 10 && x < sliderX1 + 34 && y > sliderY - 10 && y < sliderY + sliderH + 10) {
        if (_brightness < _maxBrightness) setBrightness(_brightness + _brightnessStep);
        return;
    }
}

// --- Brightness getter/setter ---

bool brightnessMenuActive() {
    return currentMenu == BRIGHTNESS_MENU;
}

int getBrightness() {
    return _brightness;
}

void setBrightness(int percent) {
    if (percent < _minBrightness) percent = _minBrightness;
    if (percent > _maxBrightness) percent = _maxBrightness;
    _brightness = percent;
    if (currentMenu == BRIGHTNESS_MENU) drawBrightnessMenu();
    _prefs.putInt("brightness", _brightness);
    setBacklightPWM(_brightness);
}

// --- Helper to display image centered on screen ---
namespace ImageDisplay {
    void displayImageCentered(const char* path, int centerX, int centerY) {
        uint16_t w = 0, h = 0;
        if (TJpgDec.getJpgSize(&w, &h, path) == 0) {
            int x = centerX - w / 2;
            int y = centerY - h / 2;
            TJpgDec.drawSdJpg(x, y, path);
        }
    }
}

void restartWiFiManager() {
    WiFiManager wm;
    wm.resetSettings();        // Forget all credentials
    delay(1000);
    ESP.restart();             // Reboot to enter config portal on next boot
}

} // namespace UI
