#include "ui_set.h"
#include "ui.h"
#include "ui_bright.h"
#include "imagedisplay.h"
#include "ui_winfo.h"
#include "wifimgr.h"

// Forward declaration of menu items
static LGFX* _tft = nullptr;
extern CST816S touch;
static bool menuVisible = false;

static const char* menuItems[] = {
    "Brightness",
    "WiFi Info",
    "Forget WiFi",
    "Back"
};
static const int menuCount = sizeof(menuItems) / sizeof(menuItems[0]);

void drawSettingsMenu() {
    _tft->fillScreen(TFT_BLACK);
    _tft->setTextDatum(middle_center);
    _tft->setTextColor(TFT_GREEN, TFT_BLACK);
    _tft->setTextSize(2);
    _tft->drawString("Type D Menu", 120, 32);

    // Draw settings items
    _tft->setTextColor(TFT_WHITE, TFT_BLACK);
    int yBase = 72;
    int itemHeight = 32;
    for (int i = 0; i < menuCount; ++i) {
        _tft->fillRoundRect(40, yBase + i * itemHeight, 160, 28, 10, TFT_DARKGREEN);
        _tft->drawRoundRect(40, yBase + i * itemHeight, 160, 28, 10, TFT_GREEN);
        if (i == menuCount - 1) {
            // Style "Back" differently
            _tft->setTextColor(TFT_GREEN, TFT_DARKGREEN);
        } else {
            _tft->setTextColor(TFT_GREEN, TFT_DARKGREEN);
        }
        _tft->setTextSize(2);
        _tft->drawString(menuItems[i], 120, yBase + i * itemHeight + 14);
    }
}

void UISet::begin(LGFX* tft) {
    _tft = tft;
    menuVisible = true;
    drawSettingsMenu();
}

bool UISet::isMenuVisible() {
    return menuVisible;
}

void UISet::update() {
    if (!menuVisible) return;

        if (touch.available()) {
        const data_struct& d = touch.data;
        if ((d.gestureID == SINGLE_CLICK || d.gestureID == NONE) && d.event == 0) {
            int yBase = 72;
            int itemHeight = 32;
            bool hit = false;
            for (int i = 0; i < menuCount; ++i) {
                int itemTop = yBase + i * itemHeight;
                // Bounding box for item
                if (d.x >= 40 && d.x <= 200 && d.y >= itemTop && d.y <= itemTop + 28) {
                    hit = true;
                    if (i == 0) {
                        Serial.println("[UISet] Triggering ui_bright_open()");
                        menuVisible = false;
                        _tft->fillScreen(TFT_BLACK);
                        ui_bright_open();
                        return;
                    } else if (i == 1) {
                        Serial.println("[UISet] Triggered ui_winfo_open()");
                            ui_winfo_open();
                            return;
                    } else if (i == 2) {
                        if (d.gestureID == LONG_PRESS){
                            Serial.println("[UISet] Forget WiFi pressed");
                            WiFiMgr::forgetWiFi();
                            menuVisible = false;
                            return;
                        }else{
                            Serial.println("[UISet] Forget WiFi: long press required");
                        }
                    } else if (i == menuCount - 1) {
                        menuVisible = false;
                        ImageDisplay::setPaused(false);
                        UI::showMenu();
                        Serial.println("[UISet] Settings menu closed (Back)");
                    }
                }
            }
            if (!hit) {
                Serial.printf("[UISet] Tap not on any item (x=%d y=%d)\n", d.x, d.y);
            }
            delay(400); // Shorter delay for better responsiveness
        }
    }
}
