#include "ui.h"
#include "disp_cfg.h"
#include "CST816S.h"
#include "imagedisplay.h"
#include "ui_set.h"
#include "ui_about.h"

static LGFX* _tft = nullptr;
CST816S touch(TOUCH_SDA, TOUCH_SCL, TOUCH_RST, TOUCH_INT);
static bool menuVisible = false;
static int menuScroll = 0;

const char* menuItems[] = {"Settings", "About"};
const int menuCount = sizeof(menuItems) / sizeof(menuItems[0]);

void UI::begin(LGFX* tft) {
    _tft = tft;
    touch.begin();
    touch.disable_auto_sleep();
    touch.enable_double_click();
    Serial.println("[UI] Touch initialized");
}

bool UI::isMenuVisible() {
    return menuVisible;
}

void UI::showMenu() {
    menuVisible = true;
    drawMenu();
    ImageDisplay::setPaused(true);
}

void UI::drawMenu() {
    _tft->setRotation(0);
    _tft->setTextDatum(middle_center);
    _tft->setTextFont(1);
    _tft->setTextSize(1);
    _tft->fillScreen(TFT_BLACK);
    _tft->setTextDatum(middle_center);
    _tft->setTextColor(TFT_GREEN, TFT_BLACK);
    _tft->setTextSize(2);
    _tft->drawString("Type D Menu", 120, 32);

    _tft->setTextColor(TFT_WHITE, TFT_BLACK);
    int yBase = 80;
    int itemHeight = 36;

    for (int i = 0; i < menuCount; ++i) {
        _tft->fillRoundRect(40, yBase + i * itemHeight, 160, 30, 10, TFT_DARKGREEN);
        _tft->drawRoundRect(40, yBase + i * itemHeight, 160, 30, 10, TFT_GREEN);
        _tft->setTextSize(2);
        _tft->setTextColor(TFT_GREEN, TFT_DARKGREEN);
        _tft->drawString(menuItems[i], 120, yBase + i * itemHeight + 15);
    }
    int exitY = yBase + menuCount * itemHeight;
    _tft->fillRoundRect(40, exitY, 160, 30, 10, TFT_BLACK);
    _tft->drawRoundRect(40, exitY, 160, 30, 10, TFT_GREEN);
    _tft->setTextSize(2);
    _tft->setTextColor(TFT_GREEN, TFT_BLACK);
    _tft->drawString("Exit", 120, exitY + 15);
}

void UI::update() {
    if (touch.available()) {
        const data_struct& d = touch.data;

        if (!menuVisible && d.gestureID == DOUBLE_CLICK) {
            menuVisible = true;
            ImageDisplay::setPaused(true);
            drawMenu();
            Serial.println("[UI] Menu opened");
            delay(400);
            return;
        }

        if (menuVisible && d.gestureID == SINGLE_CLICK) {
            int yBase = 80;
            int itemHeight = 36;
            int exitY = yBase + menuCount * itemHeight;

            // Settings (top item)
            if (d.x >= 40 && d.x <= 200 && d.y >= yBase && d.y <= yBase + 30) {
                menuVisible = false;
                UISet::begin(_tft);
                Serial.println("[UI] Settings menu opened");
                delay(400);
                return;
            }

            // About (second item)
            if (d.x >= 40 && d.x <= 200 && d.y >= yBase + itemHeight && d.y <= yBase + itemHeight + 30) {
                menuVisible = false;
                ui_about_open();
                Serial.println("[UI] About menu opened");
                delay(400);
                return;
            }

            // Exit (bottom item)
            if (d.x >= 40 && d.x <= 200 && d.y >= exitY && d.y <= exitY + 30) {
                menuVisible = false;
                ImageDisplay::setPaused(false);
                _tft->fillScreen(TFT_BLACK);
                Serial.println("[UI] Menu closed");
                delay(400);
                return;
            }
        }
        delay(750);
    }
}
