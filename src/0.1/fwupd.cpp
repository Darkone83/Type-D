// fwupd.cpp
#include "fwupd.h"
#include <SD_MMC.h>
#include <TFT_eSPI.h>
#include <Update.h>

// You must provide your tft instance from main code!
extern TFT_eSPI tft;

namespace FWUpd {

static void drawProgressBar(float progress) {
    int barWidth = 180;
    int barHeight = 22;
    int x = (TFT_WIDTH - barWidth) / 2;
    int y = (TFT_HEIGHT / 2) + 28;
    // Xbox green: #107C10
    uint16_t xboxGreen = tft.color565(0x10, 0x7C, 0x10);

    tft.drawRoundRect(x - 2, y - 2, barWidth + 4, barHeight + 4, 8, TFT_DARKGREY);
    tft.fillRect(x, y, (int)(barWidth * progress), barHeight, xboxGreen);
    tft.fillRect(x + (int)(barWidth * progress), y, barWidth - (int)(barWidth * progress), barHeight, TFT_BLACK);
    tft.drawRoundRect(x, y, barWidth, barHeight, 6, TFT_WHITE);
}

bool checkAndUpgrade() {
    File upgradeFile = SD_MMC.open("/upgrade/upgrade.bin");
    if (!upgradeFile) return false;

    // Announce upgrade to user
    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextFont(4);
    tft.drawString("Upgrade found", TFT_WIDTH/2, TFT_HEIGHT/2 - 32);
    tft.drawString("Upgrading firmware", TFT_WIDTH/2, TFT_HEIGHT/2);

    // Prepare for upgrade
    size_t updateSize = upgradeFile.size();
    size_t written = 0;
    Update.begin(updateSize);

    // Show progress bar and perform update
    uint8_t buf[4096];
    while (written < updateSize) {
        size_t toRead = min(sizeof(buf), updateSize - written);
        size_t len = upgradeFile.read(buf, toRead);
        if (len == 0) break;
        if (Update.write(buf, len) != len) {
            // Error!
            tft.setTextColor(TFT_RED, TFT_BLACK);
            tft.drawString("Update failed!", TFT_WIDTH/2, TFT_HEIGHT/2 + 60);
            upgradeFile.close();
            delay(3000);
            return false;
        }
        written += len;
        drawProgressBar((float)written / updateSize);
    }
    upgradeFile.close();

    // Finalize
    if (!Update.end(true)) {
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.drawString("Update failed!", TFT_WIDTH/2, TFT_HEIGHT/2 + 60);
        delay(3000);
        return false;
    }

    // Remove upgrade file
    SD_MMC.remove("/upgrade/upgrade.bin");
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString("Upgrade complete!", TFT_WIDTH/2, TFT_HEIGHT/2 + 60);
    delay(1000);

    // Reboot
    ESP.restart();
    return true; // Not reached
}

} // namespace FWUpd
