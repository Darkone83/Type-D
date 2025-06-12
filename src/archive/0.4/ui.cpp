#include <FS.h>
using namespace fs;
#include <WebServer.h>
#include "ui.h"
#include <Preferences.h>
#include "disp_cfg.h"
#include <Arduino.h>
#include <WiFiManager.h>
#include <SD_MMC.h>
#include "imagedisplay.h"
#include "disp_cfg.h"
#include <TJpg_Decoder.h>
#include "gallery.h"
#include "sleepmgr.h"

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

extern WiFiManager wm;
extern void restartWiFiManager();

static TFT_eSPI* _tft = nullptr;

// --- UI States ---
enum UiMenuState {
    MAIN_MENU,
    SETTINGS_MENU,
    BRIGHTNESS_MENU,
    ABOUT_MENU,
    GALLERY_MENU
};

static UiMenuState currentMenu = MAIN_MENU;

// --- Menu Selections ---
static int mainSelectedIndex = 0;
static int settingsSelectedIndex = 0;

// --- Brightness State ---
static int _brightness = 100;
static const int _minBrightness = 5;
static const int _maxBrightness = 100;
static const int _brightnessStep = 5;

static Preferences _prefs;

// Backlight pin
#define TFT_BL_PIN 32

// Slider geometry
static const int sliderY = 90;
static const int sliderX0 = 40;
static const int sliderX1 = 200;
static const int sliderW = sliderX1 - sliderX0;
static const int sliderH = 18;

// Main Menu Items
static const char* mainMenuItems[] = {
    "Settings",
    "Gallery",
    "About"
};
static const int mainMenuCount = sizeof(mainMenuItems) / sizeof(mainMenuItems[0]);

// Settings Menu Items
static const char* settingsMenuItems[] = {
    "Brightness",
    "Restart WiFi Portal",
    "Forget WiFi Network",
    "Display Sleep",
    "Back"
};
static const int settingsMenuCount = sizeof(settingsMenuItems) / sizeof(settingsMenuItems[0]);

namespace UI {

// PWM backlight
static void setupBacklightPWM() {
    pinMode(TFT_BL_PIN, OUTPUT);
}

static void setBacklightPWM(int brightnessPercent) {
    uint8_t duty = 255 - map(brightnessPercent, _minBrightness, _maxBrightness, 0, 255);
    analogWrite(TFT_BL_PIN, duty);
}

// --- Drawing Functions ---

void drawMainMenu() {
    _tft->fillScreen(_tft->color565(16, 124, 16));
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

void drawSettingsMenu() {
    _tft->fillScreen(_tft->color565(16, 124, 16));
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

// This must be non-static!
void drawBrightnessMenu() {
    _tft->fillScreen(_tft->color565(16,124,16));
    _tft->setTextDatum(MC_DATUM);
    _tft->setTextFont(4);
    _tft->setTextColor(TFT_WHITE);
    _tft->drawString("Brightness", _tft->width()/2, 35);

    _tft->fillRoundRect(sliderX0, sliderY, sliderW, sliderH, 8, TFT_DARKGREY);
    int fillW = (_brightness - _minBrightness) * (sliderW - 4) / (_maxBrightness - _minBrightness);
    if (fillW < 4) fillW = 4;
    _tft->fillRoundRect(sliderX0 + 2, sliderY + 2, fillW, sliderH - 4, 6, TFT_GREEN);

    _tft->setTextFont(4);
    _tft->setTextColor(TFT_WHITE);
    _tft->drawString("-", sliderX0 - 22, sliderY + sliderH / 2);
    _tft->drawString("+", sliderX1 + 18, sliderY + sliderH / 2);

    _tft->setTextFont(2);
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", _brightness);
    _tft->drawString(buf, _tft->width() / 2, sliderY + sliderH + 20);
}

// --- About menu implementation ---
void showAbout() {
    if (!_tft) return;

    // Fill black for now, add fades later if desired
    _tft->fillScreen(TFT_BLACK);

    const char* typeDLines[] = { "Type D", VERSION_TEXT };
    int lineHeight = 30;
    int totalHeight = lineHeight * 2;
    int startY = (_tft->height() - totalHeight) / 2;
    _tft->setTextDatum(MC_DATUM);
    _tft->setTextColor(TFT_WHITE);
    _tft->setTextFont(4);
    _tft->drawString(typeDLines[0], _tft->width() / 2, startY);
    _tft->drawString(typeDLines[1], _tft->width() / 2, startY + lineHeight);

    delay(1200);
    _tft->fillScreen(TFT_BLACK);
    _tft->drawString("Concept by: Andr0", _tft->width() / 2, _tft->height() / 2);

    delay(1200);
    _tft->fillScreen(TFT_BLACK);
    _tft->drawString("Code By: Darkone83", _tft->width() / 2, 40);
    if (SD_MMC.exists("/resources/DC.jpg"))
        ImageDisplay::displayImageCentered("/resources/DC.jpg", _tft->width() / 2, 140);
    delay(1200);

    _tft->fillScreen(TFT_BLACK);
    if (SD_MMC.exists("/resources/TR.jpg"))
        ImageDisplay::displayImageCentered("/resources/TR.jpg", _tft->width() / 2, _tft->height() / 2);
    delay(1200);

    _tft->fillScreen(TFT_BLACK);
    if (SD_MMC.exists("/resources/XBS.jpg"))
        ImageDisplay::displayImageCentered("/resources/XBS.jpg", _tft->width() / 2, _tft->height() / 2);
    delay(1200);

    // Back to main menu
    currentMenu = MAIN_MENU;
    showMenu();
}

// --- Display Sleep Menu (Stub for UI integration) ---
void showDisplaySleepMenu() {
    // Your existing code for drawing sleep menu and handling input goes here.
    // For now, just show a placeholder.
    _tft->fillScreen(TFT_BLACK);
    _tft->setTextFont(4);
    _tft->setTextDatum(MC_DATUM);
    _tft->setTextColor(TFT_WHITE);
    _tft->drawString("Display Sleep Menu", _tft->width() / 2, _tft->height() / 2);
    delay(1500);

    currentMenu = SETTINGS_MENU;
    showMenu();
}

// --- Menu visibility & exit ---
static bool _menuActive = false;
static bool _menuShouldExit = false;

bool menuVisible() { return _menuActive; }
void resetMenuExit() { _menuShouldExit = false; }
bool shouldExitMenu() { return _menuShouldExit; }

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

// --- Show menus ---
void showMenu() {
    switch (currentMenu) {
        case MAIN_MENU:        drawMainMenu(); break;
        case SETTINGS_MENU:    drawSettingsMenu(); break;
        case BRIGHTNESS_MENU:  drawBrightnessMenu(); break;
        case ABOUT_MENU:       showAbout(); break;
        case GALLERY_MENU:     Gallery::draw(); break;
    }
}

// --- Brightness getter/setter ---
bool brightnessMenuActive() { return currentMenu == BRIGHTNESS_MENU; }
int getBrightness() { return _brightness; }
void setBrightness(int percent) {
    if (percent < _minBrightness) percent = _minBrightness;
    if (percent > _maxBrightness) percent = _maxBrightness;
    _brightness = percent;
    if (currentMenu == BRIGHTNESS_MENU) drawBrightnessMenu();
    _prefs.putInt("brightness", _brightness);
    setBacklightPWM(_brightness);
}

// --- Redraw current menu/image after sleep ---
void redrawActive() {
    showMenu(); // Simple, can be extended
}

// --- Brightness slider tap ---
void brightnessMenuLoop(int16_t x, int16_t y, bool tap) {
    if (currentMenu != BRIGHTNESS_MENU || !tap) return;
    if (x > sliderX0 && x < sliderX1 && y > sliderY - 10 && y < sliderY + sliderH + 10) {
        int sliderPos = x - sliderX0;
        int newBrightness = _minBrightness + sliderPos * (_maxBrightness - _minBrightness) / (sliderW);
        setBrightness(newBrightness);
        return;
    }
    if (x > sliderX0 - 34 && x < sliderX0 - 10 && y > sliderY - 10 && y < sliderY + sliderH + 10) {
        if (_brightness > _minBrightness) setBrightness(_brightness - _brightnessStep);
        return;
    }
    if (x > sliderX1 + 10 && x < sliderX1 + 34 && y > sliderY - 10 && y < sliderY + sliderH + 10) {
        if (_brightness < _maxBrightness) setBrightness(_brightness + _brightnessStep);
        return;
    }
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
                        currentMenu = SETTINGS_MENU;
                        settingsSelectedIndex = 0;
                        showMenu();
                        break;
                    case 1: // Gallery
                        Gallery::open(Gallery::Mode::None);
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
            if (gesture == 1) {
                if (settingsSelectedIndex > 0) settingsSelectedIndex--;
                showMenu();
            } else if (gesture == 2) {
                if (settingsSelectedIndex < settingsMenuCount - 1) settingsSelectedIndex++;
                showMenu();
            } else if (gesture == 6) {
                currentMenu = MAIN_MENU;
                showMenu();
            } else if (gesture == 5) {
                switch (settingsSelectedIndex) {
                    case 0:
                        currentMenu = BRIGHTNESS_MENU;
                        showMenu();
                        break;
                    case 1:
                        restartWiFiManager();
                        currentMenu = SETTINGS_MENU;
                        showMenu();
                        break;
                    case 2:
                        wm.resetSettings();
                        restartWiFiManager();
                        currentMenu = SETTINGS_MENU;
                        showMenu();
                        break;
                    case 3:
                        showDisplaySleepMenu();
                        break;
                    case 4:
                        currentMenu = MAIN_MENU;
                        showMenu();
                        break;
                }
            }
            break;
        case BRIGHTNESS_MENU:
            // gestures handled in brightnessMenuLoop()
            break;
        case ABOUT_MENU:
            // No gesture control
            break;
        case GALLERY_MENU:
            if (gesture == 6) {
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

void restartWiFiManager() {
    WiFiManager wm;
    wm.resetSettings();
    delay(1000);
    ESP.restart();
}

} // namespace UI
