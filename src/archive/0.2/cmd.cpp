#include <FS.h>
using namespace fs;
#include <WiFiManager.h>
#include "cmd.h"
#include "ui.h"
#include "gallery.h"

namespace Cmd {

// Example help text
String help() {
    return
        "0100: Reboot\n"
        "0101: Restart WiFi Portal\n"
        "0102: Forget WiFi\n"
        "0110: About\n"
        "0120: Set Brightness\n"
        "0200: Next Image\n"
        "0201: Prev Image\n"
        "0210: Random JPEG\n"
        "0211: Random GIF\n"
        ;
}

String execute(uint16_t code, const String& arg) {
    switch (code) {
    case 0x100: // Reboot device
        ESP.restart();
        return "Rebooting";
    case 0x101: // Restart WiFi portal
        UI::restartWiFiManager();
        return "Restarting WiFi Portal";
    case 0x102: // Forget WiFi credentials
        {
            WiFiManager wm;
            wm.resetSettings();
            UI::restartWiFiManager();
        }
        return "WiFi credentials cleared";
    case 0x110: // About screen
        UI::showAbout();
        return "About";
    case 0x120: // Set brightness
        {
            int val = arg.toInt();
            if (val >= 5 && val <= 100) {
                UI::setBrightness(val);
                return "Brightness set";
            } else {
                return "Brightness out of range";
            }
        }
    case 0x200: // Next image
        Gallery::nextImage();
        return "Next image";
    case 0x201: // Prev image
        Gallery::prevImage();
        return "Previous image";
    case 0x210: // Random JPEG
        Gallery::startRandomMode(Gallery::Mode::MODE_JPG);
        return "Random JPEG";
    case 0x211: // Random GIF
        Gallery::startRandomMode(Gallery::Mode::MODE_GIF);
        return "Random GIF";
    default:
        return "Unknown command";
    }
}

} // namespace Cmd
