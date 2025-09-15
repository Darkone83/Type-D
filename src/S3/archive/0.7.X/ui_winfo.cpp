#include "ui_winfo.h"
#include "disp_cfg.h"
#include "ui_set.h"
#include <WiFi.h>

extern LGFX tft;
extern CST816S touch;

static bool menuVisible = false;


static void drawWiFiInfoMenu() {
    tft.setRotation(0);
    tft.setTextDatum(middle_center);
    tft.setTextFont(1);
    tft.setTextSize(1);
    tft.fillScreen(TFT_BLACK);

    // Title
    tft.setTextDatum(middle_center);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setTextSize(2);
    tft.drawString("WiFi Info", 120, 36);

    // SSID (centered, white)
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    String ssid = WiFi.SSID();
    tft.drawString(ssid.length() > 0 ? ssid : "(none)", 120, 95);

    // IP (centered, white, just below SSID)
    IPAddress ip = WiFi.localIP();
    String ipStr = ip.toString();
    tft.drawString(ipStr, 120, 130);

    // Back button
    int backW = 120, backH = 38, backX = 60, backY = 180;
    tft.setTextSize(2);
    tft.fillRoundRect(backX, backY, backW, backH, 10, TFT_DARKGREEN);
    tft.drawRoundRect(backX, backY, backW, backH, 10, TFT_GREEN);
    tft.setTextColor(TFT_GREEN, TFT_DARKGREEN);
    tft.drawString("Back", backX + backW/2, backY + backH/2);

    tft.setTextSize(2);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
}

void ui_winfo_open() {
    menuVisible = true;
    drawWiFiInfoMenu();
}

void ui_winfo_exit() {
    menuVisible = false;
    tft.fillScreen(TFT_BLACK);
    // Return to settings menu
    UISet::begin(&tft);
}

bool ui_winfo_isVisible() {
    return menuVisible;
}

void ui_winfo_update() {
    if (!menuVisible) return;

    if (touch.available()) {
        const data_struct& d = touch.data;

        if ((d.gestureID == SINGLE_CLICK || d.gestureID == NONE) && d.event == 0) {
            int backW = 120, backH = 38, backX = 60, backY = 180;

            if (d.x >= backX && d.x < backX + backW && d.y >= backY && d.y < backY + backH) {
                Serial.println("[ui_winfo_update] Back button pressed");
                menuVisible = false;
                UISet::begin(&tft);
                delay(400);
            }
        }
    }
}
