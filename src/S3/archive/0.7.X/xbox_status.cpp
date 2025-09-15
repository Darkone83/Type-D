#include "xbox_status.h"
#include <FFat.h>
#include "disp_cfg.h"

void drawShadowedText(LGFX* tft, const String& text, int x, int y, uint16_t color, uint16_t shadow, int font) {
    tft->setTextFont(font);
    tft->setTextColor(shadow, TFT_BLACK);
    tft->drawString(text, x+2, y+2);
    tft->setTextColor(color, TFT_BLACK);
    tft->drawString(text, x, y);
}

namespace xbox_status {

void show(LGFX* tft, const XboxStatus& packet) {
    // ---- ENSURE DISPLAY STATE RESET ----
    tft->setRotation(0);         // Always reset rotation
    tft->setTextDatum(TL_DATUM); // Use TL_DATUM for this overlay
    tft->setTextFont(1);         // Default font
    tft->setTextSize(1);         // Default size

    tft->fillScreen(TFT_BLACK);

    // Pyramid layout
    const int centerX = tft->width() / 2;
    const int topY = 30;
    const int bottomY = 130;
    const int pyramidOffsetX = 70;
    const int iconSize = 40;
    const int labelFont = 2;
    const int valueFont = 2;

    uint16_t labelCol = TFT_LIGHTGREY;
    uint16_t valueCol = 0x07E0;

    struct StatusItem {
        const char* icon;
        String label;
        String value;
        int x, y;
    } items[] = {
        // Fan: Top center
        { "/resource/fan.jpg",  "Fan",     String(packet.fanSpeed), centerX, topY },
        // CPU: Bottom left
        { "/resource/cpu.jpg",  "CPU",     String(packet.cpuTemp) + "C", centerX - pyramidOffsetX, bottomY },
        // Ambient: Bottom right
        { "/resource/amb.jpg",  "Ambient", String(packet.ambientTemp) + "C", centerX + pyramidOffsetX, bottomY }
        // App is commented/removed for now
        // { "/resource/app.jpg",  "App",     String(packet.currentApp), ... }
    };

    for (int i = 0; i < 3; ++i) {
        int iconX = items[i].x - iconSize / 2;
        int iconY = items[i].y;

        // Draw icon centered
        File iconFile = FFat.open(items[i].icon, "r");
        if (iconFile && iconFile.size() > 0) {
            size_t jpgSize = iconFile.size();
            uint8_t* jpgBuffer = (uint8_t*)heap_caps_malloc(jpgSize, MALLOC_CAP_SPIRAM);
            if (jpgBuffer) {
                int bytesRead = iconFile.read(jpgBuffer, jpgSize);
                iconFile.close();
                if ((size_t)bytesRead == jpgSize) {
                    tft->drawJpg(jpgBuffer, jpgSize, iconX, iconY, iconSize, iconSize);
                }
                heap_caps_free(jpgBuffer);
            } else {
                iconFile.close();
            }
        } else {
            if (iconFile) iconFile.close();
        }

        // Center label under icon
        int labelY = iconY + iconSize + 2;
        int labelX = items[i].x - tft->textWidth(items[i].label) / 2;
        drawShadowedText(tft, items[i].label, labelX, labelY, labelCol, TFT_DARKGREY, labelFont);

        // Center value under label
        int valueY = labelY + 18;
        int valueX = items[i].x - tft->textWidth(items[i].value) / 2;
        drawShadowedText(tft, items[i].value, valueX, valueY, valueCol, TFT_DARKGREY, valueFont);
    }
}

} // namespace xbox_status
