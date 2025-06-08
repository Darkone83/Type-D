#include "xbox_status.h"

// Helper for Xbox drop-shadow effect
static void drawShadowedText(TFT_eSPI* tft, const String& text, int x, int y, uint16_t color, uint16_t shadow = TFT_DARKGREY, int font = 4) {
    tft->setTextFont(font);
    tft->setTextColor(shadow, TFT_BLACK);
    tft->drawString(text, x + 2, y + 2);
    tft->setTextColor(color, TFT_BLACK);
    tft->drawString(text, x, y);
}

namespace xbox_status {
void show(TFT_eSPI* tft, const XboxPacket& packet) {
    tft->fillRect(0, 0, tft->width(), tft->height(), TFT_BLACK);

    // Layout grid (assumes 240x240 or similar)
    const int boxW = tft->width() / 2;
    const int boxH = tft->height() / 2;

    // Colors
    uint16_t labelCol = TFT_GREEN;
    uint16_t valueCol = TFT_WHITE;

    // Top-left: Fan
    drawShadowedText(tft, "FAN", 12, 16, labelCol, TFT_DARKGREY, 2);
    drawShadowedText(tft, String(packet.fanSpeed), 24, 44, valueCol, TFT_DARKGREY, 4);

    // Top-right: CPU Temp
    String cpuLabel = "CPU";
    int cpuX = tft->width() - 60;
    drawShadowedText(tft, cpuLabel, cpuX, 16, labelCol, TFT_DARKGREY, 2);
    drawShadowedText(tft, String(packet.cpuTemp) + "C", cpuX+10, 44, valueCol, TFT_DARKGREY, 4);

    // Bottom-left: Ambient
    drawShadowedText(tft, "AMB", 12, tft->height()/2 + 16, labelCol, TFT_DARKGREY, 2);
    drawShadowedText(tft, String(packet.ambientTemp) + "C", 24, tft->height()/2 + 44, valueCol, TFT_DARKGREY, 4);

    // Bottom-right: App
    String appStr = String(packet.app);
    int appX = tft->width() - 60;
    drawShadowedText(tft, "APP", appX, tft->height()/2 + 16, labelCol, TFT_DARKGREY, 2);
    // Truncate/pad app name if needed
    if (appStr.length() > 12) appStr = appStr.substring(0, 12) + "...";
    drawShadowedText(tft, appStr, appX+10, tft->height()/2 + 44, valueCol, TFT_DARKGREY, 2);
}
}
