#include "ui_about.h"
#include "ui.h"
#include "disp_cfg.h"
#include <FFat.h>
#include "imagedisplay.h"

extern LGFX tft;

static bool aboutActive = false;

static constexpr uint16_t COLOR_GREEN  = TFT_GREEN;
static constexpr uint16_t COLOR_WHITE  = TFT_WHITE;
static constexpr uint16_t COLOR_YELLOW = 0xFFE0; // Yellow
static constexpr uint16_t COLOR_RED    = 0xF800; // Red
static constexpr uint16_t COLOR_PURPLE = 0x780F; // Purple

// ---- Minimal JPEG dimension parser ----
bool decodeJpegSize(const uint8_t* jpg, size_t len, uint16_t* w, uint16_t* h) {
    for (size_t i = 0; i + 9 < len; ++i) {
        // Look for Baseline SOF0 marker
        if (jpg[i] == 0xFF && jpg[i+1] == 0xC0) {
            *h = (jpg[i+5] << 8) | jpg[i+6];
            *w = (jpg[i+7] << 8) | jpg[i+8];
            return true;
        }
    }
    return false;
}

// ---- Fade-to-black transition ----
void about_fadeToBlack(int steps = 10, int delayMs = 15) {
    for (int i = 0; i < steps; ++i) {
        uint8_t shade = 255 - (i * (255 / steps));
        tft.fillRect(0, 0, tft.width(), tft.height(), tft.color565(shade, shade, shade));
        delay(delayMs);
    }
    tft.fillScreen(TFT_BLACK);
}

// ---- Draw and center JPG from FFat ----
void about_drawImageCentered(const char* path) {
    File jpgFile = FFat.open(path, "r");
    if (jpgFile && jpgFile.size() > 0) {
        size_t jpgSize = jpgFile.size();
        uint8_t* jpgBuffer = (uint8_t*)heap_caps_malloc(jpgSize, MALLOC_CAP_SPIRAM);
        if (jpgBuffer) {
            int bytesRead = jpgFile.read(jpgBuffer, jpgSize);
            jpgFile.close();
            if ((size_t)bytesRead == jpgSize) {
                uint16_t w = 0, h = 0;
                if (decodeJpegSize(jpgBuffer, jpgSize, &w, &h)) {
                    int x = (tft.width()  - w) / 2;
                    int y = (tft.height() - h) / 2;
                    tft.drawJpg(jpgBuffer, jpgSize, x, y);
                } else {
                    // fallback if JPEG parse fails
                    tft.drawJpg(jpgBuffer, jpgSize, 0, 0);
                }
            }
            heap_caps_free(jpgBuffer);
        } else {
            jpgFile.close();
            Serial.println("[About] PSRAM alloc failed!");
        }
    } else {
        Serial.println("[About] JPG file open or size failed!");
        if (jpgFile) jpgFile.close();
    }
}

// ---- About menu animation ----
void ui_about_open() {
    aboutActive = true;
}

bool ui_about_isActive() {
    return aboutActive;
}

void ui_about_update() {
    if (!aboutActive) return;

    // --- Fade to black
    about_fadeToBlack();

    // --- Type D + Version ---
    tft.setTextDatum(middle_center);
    tft.setTextColor(COLOR_GREEN, TFT_BLACK);
    tft.setTextSize(3);
    tft.drawString("Type D", tft.width() / 2, tft.height() / 2 - 24);
    tft.setTextColor(COLOR_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.drawString(VERSION_TEXT, tft.width() / 2, tft.height() / 2 + 18);
    delay(1500);

    // --- Fade to black
    about_fadeToBlack();

    // --- Concept by: Andr0 ---
    tft.setTextColor(COLOR_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.drawString("Concept by:", tft.width() / 2, tft.height() / 2 - 18);
    tft.setTextColor(COLOR_YELLOW, TFT_BLACK);
    tft.setTextSize(3);
    tft.drawString("Andr", tft.width() / 2 - 24, tft.height() / 2 + 18);
    tft.setTextColor(COLOR_RED, TFT_BLACK);
    tft.drawString("0", tft.width() / 2 + 30, tft.height() / 2 + 18);
    delay(1500);

    // --- Fade to black
    about_fadeToBlack();

    // --- Coded by: Darkone83 ---
    tft.setTextColor(COLOR_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.drawString("Coded by:", tft.width() / 2, tft.height() / 2 - 18);
    tft.setTextColor(COLOR_PURPLE, TFT_BLACK);
    tft.setTextSize(3);
    tft.drawString("Darkone", tft.width() / 2 - 30, tft.height() / 2 + 18);
    tft.setTextColor(COLOR_GREEN, TFT_BLACK);
    tft.drawString("83", tft.width() / 2 + 56, tft.height() / 2 + 18);
    delay(1500);

    // --- Fade to black
    about_fadeToBlack();

    // --- Show /resource/TR.jpg ---
    about_drawImageCentered("/resource/TR.jpg");
    delay(1500);

    // --- Fade to black
    about_fadeToBlack();

    // --- Show /resource/XBS.jpg + "XBOX-scene.info" ---
    about_drawImageCentered("/resource/XBS.jpg");
    tft.setTextDatum(middle_center);
    tft.setTextColor(COLOR_WHITE, TFT_BLACK);
    tft.setTextSize(1);
    tft.drawString("XBOX-scene.info", tft.width() / 2, tft.height() - 32);
    delay(1500);

    // --- Fade to black
    about_fadeToBlack();

    // --- Show /resource/DC.jpg ---
    about_drawImageCentered("/resource/DC.jpg");
    tft.setTextColor(COLOR_WHITE, TFT_BLACK);
    tft.setTextSize(1);
    tft.drawString("darkonecustoms.com", tft.width() / 2, tft.height() - 32);
    delay(2000);

    // --- Fade to black
    about_fadeToBlack();

        // --- Show /resource/TD.jpg ---
    about_drawImageCentered("/resource/TD.jpg");
    tft.setTextColor(COLOR_WHITE, TFT_BLACK);
    delay(4000);

    // --- Fade to black
    about_fadeToBlack();

    // --- Return to menu ---
    aboutActive = false;
    UI::showMenu();
}
