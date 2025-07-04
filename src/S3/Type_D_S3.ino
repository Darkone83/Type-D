/////////////////////////
//    Type D Firmware  //
//  Code by: Darkone83 //
// Concept by: Andr0   //
// Special Thanks to:  //
//   Team Resurgent    //
//     XBOX Scene      //
//  And the modding    //
//     community       //
/////////////////////////

#include <Arduino.h>
#include "wifimgr.h"
#include <FFat.h>
#include "disp_cfg.h"
#include "detect.h"
#include "fileman.h"
#include "imagedisplay.h"
#include "boot.h"
#include <ESPAsyncWebServer.h>
#include "xbox_status.h"
#include "ui.h"
#include "ui_set.h"
#include "ui_bright.h"
#include "ui_about.h"
#include <Preferences.h>
#include "cmd.h"
#include "diag.h"
#include "udp_detect.h"

#define WIFI_TIMEOUT 120
#define BRIGHTNESS_PREF_KEY "brightness"
#define BRIGHTNESS_PREF_NS "type_d"

LGFX tft;
AsyncWebServer server80(80);
AsyncWebServer server8080(8080);


bool nowConnected = WiFiMgr::isConnected();
bool portalInfoShown = false;

static bool overlayPending = false;
static bool showingXboxStatus = false;
static unsigned long lastStatusDisplay = 0;

XboxStatus lastXboxStatus;

static int percent_to_hw(int percent) {
    if (percent < 5) percent = 5;
    if (percent > 100) percent = 100;
    return ((percent * 255) / 100);
}

void apply_brightness_on_boot() {
    Preferences prefs;
    prefs.begin(BRIGHTNESS_PREF_NS, true); // read-only
    int brightness = prefs.getUInt(BRIGHTNESS_PREF_KEY, 100); // default 100%
    prefs.end();
    if (brightness < 5) brightness = 5;
    if (brightness > 100) brightness = 100;
    tft.setBrightness(percent_to_hw(brightness));
}

// -- Show WiFi Portal Info ONLY if portal is active (not connected) --
void displayPortalInfo() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setTextDatum(MC_DATUM); // center text

    tft.setTextSize(2);
    tft.drawString("WiFi Portal Active", tft.width()/2, tft.height()/2 - 30);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.drawString("Type D Setup", tft.width()/2, tft.height()/2);
    tft.drawString("IP: 192.168.4.1", tft.width()/2, tft.height()/2 + 16);
    tft.setTextSize(1);
    tft.drawString("Connect below to setup.", tft.width()/2, tft.height()/2 + 32);
}

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("[Type D] Booting...");

    tft.init();
    tft.setRotation(0);
    apply_brightness_on_boot();
    tft.fillScreen(TFT_BLACK);

    ImageDisplay::begin(&tft);

    if (!FFat.begin()) {
        Serial.println("[Type D] FFat Mount Failed! Attempting to format...");
        if (FFat.format()) {
            Serial.println("[Type D] FFat format succeeded! Rebooting...");
            delay(1000);
            ESP.restart();
        } else {
            Serial.println("[Type D] FFat format FAILED! Halting.");
            while (1) delay(100);
        }
    } else {
        Serial.println("[Type D] FFat Mounted OK.");
    }
    server80.serveStatic("/resource/", FFat, "/resource/");
    server8080.serveStatic("/resource/", FFat, "/resource/");

    // --- BOOT ANIMATION ---
    bootShowScreen();

    // ---- SPLASH TEXT ----
    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(middle_center);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setTextSize(3);
    tft.drawString("Type D", tft.width() / 2, tft.height() / 2 - 18);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.drawString(VERSION_TEXT, tft.width() / 2, tft.height() / 2 + 10);
    delay(1500);

    // WiFiMgr replaces WiFiManager
    WiFiMgr::begin();
    Serial.println("[Type D] WiFiMgr initialized.");

    // If not connected, display portal info
    if (!WiFiMgr::isConnected()) {
        displayPortalInfo();
    }

    UDPDetect::begin();

    // --- Start detection and web server modules ---
    Detect::begin();
    server8080.begin();
    FileMan::begin(server8080);
    Diag::begin(server8080);
    cmd_init(&server8080, &tft);
    UI::begin(&tft);

    Serial.printf("[Type D] Device ID: %d\n", Detect::getId());

    // --- Show a random image on WiFi success as well ---
    ImageDisplay::displayRandomImage();
}

void loop() {
    WiFiMgr::loop();

    // 1. Highest priority: About, brightness, menu overlays
    if (ui_about_isActive()) { ui_about_update(); return; }
    if (ui_bright_isVisible()) { ui_bright_update(); return; }
    if (UISet::isMenuVisible()) { UISet::update(); return; }
    UI::update();

    // 2. Run detection and UDP polling
    Detect::loop();
    UDPDetect::loop();

    // 3. Status overlay logic -- only show between images and if no UI/menu overlay is active
    bool anyUiActive = ui_about_isActive() || ui_bright_isVisible() || UISet::isMenuVisible() || UI::isMenuVisible();

    if (ImageDisplay::isDone() && UDPDetect::hasPacket() && !overlayPending && !showingXboxStatus && !anyUiActive) {
        lastXboxStatus = UDPDetect::getLatest(); // latch latest
        overlayPending = true;
        UDPDetect::acknowledge();
    }

    // Show overlay if pending (takes precedence over image display)
    if (overlayPending && !anyUiActive) {
        xbox_status::show(&tft, lastXboxStatus);
        lastStatusDisplay = millis();
        showingXboxStatus = true;
        overlayPending = false;
        return; // Show overlay, block image update
    }

    // If overlay is showing, time it out after 2s, then resume slideshow
    if (showingXboxStatus && !anyUiActive) {
        if (millis() - lastStatusDisplay > 2000) {
            showingXboxStatus = false;
            ImageDisplay::displayRandomImage();
        }
        return; // Block image update while overlay active
    }

    // 4. Only update image if overlay is not showing and not in a menu
    if (!UI::isMenuVisible()) {
        ImageDisplay::update();
    }

    cmd_serial_poll();
    
}

