// xbox_status.cpp
#include "xbox_status.h"
#include <TJpg_Decoder.h>
#include <TFT_eSPI.h>

void drawShadowedText(TFT_eSPI* tft, const String& text, int x, int y, uint16_t color, uint16_t shadow, int font) {
    tft->setTextFont(font);
    tft->setTextColor(shadow, TFT_BLACK);
    tft->drawString(text, x+2, y+2);
    tft->setTextColor(color, TFT_BLACK);
    tft->drawString(text, x, y);
}

namespace xbox_status {

void show(TFT_eSPI* tft, const XboxPacket& packet) {
    tft->fillScreen(TFT_BLACK);

    const int iconSize = 32;
    const int rowH = 48; // Space between each status row
    const int startY = 8;
    const int iconX = 8;
    const int labelX = iconX + iconSize + 8;   // Icon + margin
    const int valueX = labelX + 80;            // Label + margin (adjust as needed)

    // Colors
    uint16_t labelCol = TFT_LIGHTGREY;
    uint16_t valueCol = 0x07E0; // Xbox green

    struct StatusRow {
        const char* icon;
        String label;
        String value;
    } rows[] = {
        { "/resources/fan.jpg",  "Fan",     String(packet.fanSpeed) },
        { "/resources/cpu.jpg",  "CPU",     String(packet.cpuTemp) + "C" },
        { "/resources/amb.jpg",  "Ambient", String(packet.ambientTemp) + "C" },
        { "/resources/app.jpg",  "App",     String(packet.app) }
    };

    for (int i = 0; i < 4; ++i) {
        int y = startY + i * rowH;
        // Draw icon at left
        TJpgDec.drawSdJpg(iconX, y, rows[i].icon);

        // Vertically center text with icon: font 4 is ~24px tall
        int textY = y + (iconSize / 2) - 12; // -12 for font baseline
        drawShadowedText(tft, rows[i].label, labelX, textY, labelCol, TFT_DARKGREY, 4);
        drawShadowedText(tft, rows[i].value, valueX, textY, valueCol, TFT_DARKGREY, 4);
    }
}

} // namespace xbox_status
