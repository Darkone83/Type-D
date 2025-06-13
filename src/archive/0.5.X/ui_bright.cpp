#include "ui_bright.h"
#include "disp_cfg.h"
#include <Arduino.h>
#include <Preferences.h>
#include "ui_set.h" // For returning to settings menu

extern LGFX tft;
extern CST816S touch;

static bool menuVisible = false;
static Preferences prefs;

#define BRIGHTNESS_PREF_KEY "brightness"
#define BRIGHTNESS_PREF_NS "type_d"

enum BrightnessLevel { BRIGHT_HIGH, BRIGHT_MED, BRIGHT_LOW };
static BrightnessLevel currLevel = BRIGHT_HIGH;

const int brightPercents[3] = {100, 60, 15};
const char* brightLabels[3] = {"High", "Med", "Low"};

static int percent_to_hw(int percent) {
    if (percent < 5) percent = 5;
    if (percent > 100) percent = 100;
    return ((percent * 255) / 100);
}

static void apply_brightness(BrightnessLevel level) {
    int percent = brightPercents[level];
    int hwval = percent_to_hw(percent);
    Serial.printf("[ui_bright_update] setBrightness(%d)\n", hwval);
    tft.setBrightness(hwval);
    prefs.begin(BRIGHTNESS_PREF_NS, false); // read-write
    prefs.putUInt(BRIGHTNESS_PREF_KEY, percent);
    prefs.end();
}

static void drawBrightnessMenu() {
    tft.fillScreen(TFT_BLACK);

    // Title
    tft.setTextDatum(middle_center);
    tft.setTextSize(2);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString("Brightness", 120, 36);

    // Large brightness button
    int btnW = 180, btnH = 56, btnX = 30, btnY = 80, radius = 18;
    tft.fillRoundRect(btnX, btnY, btnW, btnH, radius, TFT_DARKGREEN);
    tft.drawRoundRect(btnX, btnY, btnW, btnH, radius, TFT_GREEN);
    tft.setTextDatum(middle_center);
    tft.setTextSize(3);
    tft.setTextColor(TFT_GREEN, TFT_DARKGREEN);
    tft.drawString(brightLabels[currLevel], btnX + btnW / 2, btnY + btnH / 2);

    // Back button (centered below)
    int backW = 120, backH = 38, backX = 60, backY = btnY + btnH + 28;
    tft.setTextSize(2);
    tft.fillRoundRect(backX, backY, backW, backH, 10, TFT_DARKGREEN);
    tft.drawRoundRect(backX, backY, backW, backH, 10, TFT_GREEN);
    tft.setTextColor(TFT_GREEN, TFT_DARKGREEN); // Match fill, no black box
    tft.drawString("Back", backX + backW / 2, backY + backH / 2);

    tft.setTextSize(2);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
}

void ui_bright_open() {
    prefs.begin(BRIGHTNESS_PREF_NS, true); // read-only
    int lastPercent = prefs.getUInt(BRIGHTNESS_PREF_KEY, 100);
    prefs.end();
    if (lastPercent >= 80)      currLevel = BRIGHT_HIGH;
    else if (lastPercent >= 40) currLevel = BRIGHT_MED;
    else                        currLevel = BRIGHT_LOW;

    menuVisible = true;
    apply_brightness(currLevel);
    drawBrightnessMenu();
}

void ui_bright_exit() {
    menuVisible = false;
    tft.fillScreen(TFT_BLACK);
}

bool ui_bright_isVisible() {
    return menuVisible;
}

void ui_bright_update() {
    if (!menuVisible) return;

    if (touch.available()) {
        const data_struct& d = touch.data;
        Serial.printf("[ui_bright_update] gesture: %u event: %u x:%d y:%d\n", d.gestureID, d.event, d.x, d.y);

        if ((d.gestureID == SINGLE_CLICK || d.gestureID == NONE) && d.event == 0) {
            int btnW = 180, btnH = 56, btnX = 30, btnY = 80;
            int backW = 120, backH = 38, backX = 60, backY = btnY + btnH + 28;

            if (d.x >= btnX && d.x < btnX + btnW && d.y >= btnY && d.y < btnY + btnH) {
                Serial.println("[ui_bright_update] Brightness button pressed");
                currLevel = (BrightnessLevel)((currLevel + 1) % 3);
                apply_brightness(currLevel);
                drawBrightnessMenu();
                delay(400);
            }
            else if (d.x >= backX && d.x < backX + backW && d.y >= backY && d.y < backY + backH) {
                Serial.println("[ui_bright_update] Back button pressed");
                menuVisible = false;
                // Return to settings menu
                UISet::begin(&tft);
                delay(400);
            } else {
                Serial.printf("[ui_bright_update] Touch was outside buttons: x=%d y=%d\n", d.x, d.y);
            }
        }
    }
}
