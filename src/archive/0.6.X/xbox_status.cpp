#include "xbox_status.h"
#include <FFat.h>
#include "disp_cfg.h"

// Ported shadowed text helper
void drawShadowedText(LGFX* tft, const String& text, int x, int y, uint16_t color, uint16_t shadow, int font) {
    tft->setTextFont(font);
    tft->setTextColor(shadow, TFT_BLACK);
    tft->drawString(text, x+2, y+2);
    tft->setTextColor(color, TFT_BLACK);
    tft->drawString(text, x, y);
}

namespace xbox_status {

void show(LGFX* tft, const XboxPacket& packet) {
    tft->fillScreen(TFT_BLACK);

    const int iconSize = 32;
    const int rowH = 48;
    const int startY = 8;
    const int iconX = 8;
    const int labelX = iconX + iconSize + 8;
    const int valueX = labelX + 80;

    uint16_t labelCol = TFT_LIGHTGREY;
    uint16_t valueCol = 0x07E0;

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

        File iconFile = FFat.open(rows[i].icon, "r");
        if (iconFile && iconFile.size() > 0) {
            size_t jpgSize = iconFile.size();
            uint8_t* jpgBuffer = (uint8_t*)heap_caps_malloc(jpgSize, MALLOC_CAP_SPIRAM);
            if (jpgBuffer) {
                int bytesRead = iconFile.read(jpgBuffer, jpgSize);
                iconFile.close();
                if ((size_t)bytesRead == jpgSize) {
                    tft->drawJpg(jpgBuffer, jpgSize, iconX, y, iconSize, iconSize);
                }
                heap_caps_free(jpgBuffer);
            } else {
                iconFile.close();
            }
        } else {
            if (iconFile) iconFile.close();
            // Optionally draw a blank or error icon here
        }

        int textY = y + (iconSize / 2) - 12;
        drawShadowedText(tft, rows[i].label, labelX, textY, labelCol, TFT_DARKGREY, 4);
        drawShadowedText(tft, rows[i].value, valueX, textY, valueCol, TFT_DARKGREY, 4);
    }
}

} // namespace xbox_status
