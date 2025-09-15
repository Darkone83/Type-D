#include "xbox_status.h"
#include <FFat.h>
#include "disp_cfg.h"
#include <esp_heap_caps.h>   // for heap_caps_malloc/free

static void drawShadowedText(LGFX* tft, const String& text, int x, int y,
                             uint16_t color, uint16_t shadow, int font) {
    tft->setTextFont(font);
    tft->setTextColor(shadow, TFT_BLACK);
    tft->drawString(text, x+2, y+2);
    tft->setTextColor(color, TFT_BLACK);
    tft->drawString(text, x, y);
}

static void drawIconOrPlaceholder(LGFX* tft, const char* path,
                                  int x, int y, int w, int h) {
    File f = FFat.open(path, "r");
    if (f && f.size() > 0) {
        size_t sz = f.size();
        uint8_t* buf = (uint8_t*)heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
        if (buf) {
            int n = f.read(buf, sz);
            f.close();
            if (n == (int)sz) {
                tft->drawJpg(buf, sz, x, y, w, h);
            }
            heap_caps_free(buf);
        } else {
            f.close();
        }
    } else {
        if (f) f.close();
        tft->fillRoundRect(x, y, w, h, 6, TFT_DARKGREY);
        tft->drawRoundRect(x, y, w, h, 6, TFT_BLACK);
    }
}

// --- Pretty print resolution like PC viewer script ---
static String prettyResolution(int w, int h) {
    String val = String(w) + "x" + String(h);
    if (w == 640 && h == 480)       val += " (480p)";
    else if (w == 720 && h == 480)  val += " (480p WS)";
    else if (w == 1280 && h == 720) val += " (720p)";
    else if (w == 1920 && h == 1080)val += " (1080i)";
    return val;
}

namespace xbox_status {

// page flip timing
static const uint32_t PAGE_MS = 4000;
static uint32_t s_lastFlip = 0;
static bool s_showPrimary = true;

void show(LGFX* tft, const XboxStatus& packet) {
    // flip page if interval elapsed
    uint32_t now = millis();
    if (now - s_lastFlip >= PAGE_MS) {
        s_lastFlip = now;
        s_showPrimary = !s_showPrimary;
    }

    // reset draw state
    tft->setRotation(0);
    tft->setTextDatum(TL_DATUM);
    tft->setTextFont(1);
    tft->setTextSize(1);
    tft->fillScreen(TFT_BLACK);

    const int W = tft->width();
    const int H = tft->height();
    const int CX = W / 2;
    const int CY = H / 2;
    const int iconSize = 36;
    const int labelFont = 2;
    const int valueFont = 2;
    const uint16_t labelCol = TFT_LIGHTGREY;
    const uint16_t valueCol = 0x07E0;

    if (s_showPrimary) {
        // -------- Page A: Fan / CPU / Ambient --------
        const int topY = CY - 64;
        const int botY = CY + 38;
        const int offX = 70;

        struct Item {
            const char* icon;
            String label;
            String value;
            int x, y;
        } items[] = {
            { "/resource/fan.jpg", "Fan",     String(packet.fanSpeed),           CX,           topY },
            { "/resource/cpu.jpg", "CPU",     String(packet.cpuTemp) + "C",      CX - offX,    botY },
            { "/resource/amb.jpg", "Ambient", String(packet.ambientTemp) + "C",  CX + offX,    botY },
        };

        for (int i = 0; i < 3; ++i) {
            int iconX = items[i].x - iconSize / 2;
            int iconY = items[i].y - iconSize / 2;

            drawIconOrPlaceholder(tft, items[i].icon, iconX, iconY, iconSize, iconSize);

            int labelY = iconY + iconSize + 2;
            int labelX = items[i].x - tft->textWidth(items[i].label) / 2;
            drawShadowedText(tft, items[i].label, labelX, labelY, labelCol, TFT_DARKGREY, labelFont);

            int valueY = labelY + 18;
            int valueX = items[i].x - tft->textWidth(items[i].value) / 2;
            drawShadowedText(tft, items[i].value, valueX, valueY, valueCol, TFT_DARKGREY, valueFont);
        }
    } else {
        // -------- Page B: App / Resolution --------
        String app = String(packet.currentApp);
        if (app.length() == 0) app = "Unknown";

        String res;
        if (packet.videoWidth > 0 && packet.videoHeight > 0) {
            res = prettyResolution(packet.videoWidth, packet.videoHeight);
        } else if (strlen(packet.resolution) > 0) {
            res = String(packet.resolution);
        } else {
            res = "—";
        }

        const int gapRows = 18;
        const int rowH = iconSize + 2 + 16 + 18 + 16; // icon + label + gap + value
        const int totalH = rowH * 2 + gapRows;
        int startY = (H - totalH) / 2;
        if (startY < 8) startY = 8;

        struct Row { const char* icon; String label; String value; } rows[2] = {
            { "/resource/app.jpg", "App",        app },
            { "/resource/res.jpg", "Resolution", res },
        };

        for (int i = 0; i < 2; ++i) {
            int rowYTop = startY + i * (rowH + (i == 0 ? gapRows : 0));
            int iconX   = (W - iconSize) / 2;
            int iconY   = rowYTop;

            drawIconOrPlaceholder(tft, rows[i].icon, iconX, iconY, iconSize, iconSize);

            int labelY = iconY + iconSize + 2;
            int labelW = tft->textWidth(rows[i].label);
            int labelX = (W - labelW) / 2;
            drawShadowedText(tft, rows[i].label, labelX, labelY, labelCol, TFT_DARKGREY, labelFont);

            int valueY = labelY + 18;
            int valueW = tft->textWidth(rows[i].value);
            int valueX = (W - valueW) / 2;
            drawShadowedText(tft, rows[i].value, valueX, valueY, valueCol, TFT_DARKGREY, valueFont);
        }
    }
}

} // namespace xbox_status
