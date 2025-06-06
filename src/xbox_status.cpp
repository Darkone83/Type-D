#include "xbox_status.h"

// Helper: draw text with drop shadow for Xbox look
static void drawShadowText(TFT_eSPI* tft, const char* str, int x, int y, int font, uint16_t color, uint16_t shadow = TFT_DARKGREY) {
    tft->setTextFont(font);
    tft->setTextColor(shadow);
    tft->drawString(str, x + 2, y + 2);
    tft->setTextColor(color);
    tft->drawString(str, x, y);
}

namespace xbox_status {

void show(TFT_eSPI* tft, const XboxStatus& status) {
    tft->fillScreen(TFT_BLACK);

    int W = tft->width(), H = tft->height();

    // --- Top bar: Fan (left), CPU (right) ---
    tft->setTextDatum(TL_DATUM);
    char buf[32];

    // Fan (top left)
    snprintf(buf, sizeof(buf), "Fan: %d", status.fanSpeed);
    drawShadowText(tft, buf, 8, 8, 2, tft->color565(16, 255, 16));

    // CPU (top right)
    snprintf(buf, sizeof(buf), "CPU: %dC", status.cpuTemp);
    tft->setTextDatum(TR_DATUM);
    drawShadowText(tft, buf, W - 8, 8, 2, tft->color565(16, 255, 16));

    // --- IP (centered, below top bar) ---
    tft->setTextDatum(MC_DATUM);
    tft->setTextFont(4);
    snprintf(buf, sizeof(buf), "IP: %s", status.ipAddress);
    drawShadowText(tft, buf, W / 2, H / 2 - 30, 4, TFT_WHITE);

    // --- App/Title (centered near bottom) ---
    tft->setTextFont(4);
    snprintf(buf, sizeof(buf), "%s", status.currentApp[0] ? status.currentApp : "<No Title>");
    drawShadowText(tft, buf, W / 2, H - 48, 4, tft->color565(16, 255, 16));
}

} // namespace xbox_status
