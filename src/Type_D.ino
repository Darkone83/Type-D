/////////////////////////
//    Type D Firmware  //
//  Code by: Darkone83 //
// Concept by: Andr0   //
// Special Thanks to:  //
//   Team Resurgent    //
//     XBOX Scene      //
//  And the modding    //
//     comminuty       //
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
#include "espnow_receiver.h"
#include "xbox_status.h"
#include "ui.h"
#include "ui_set.h"
#include "ui_bright.h"
#include "ui_about.h"
#include <Preferences.h>
#include "cmd.h"

#define WIFI_TIMEOUT 120
#define BRIGHTNESS_PREF_KEY "brightness"
#define BRIGHTNESS_PREF_NS "type_d"

LGFX tft;
AsyncWebServer server80(80);
AsyncWebServer server8080(8080);

bool nowConnected = WiFiMgr::isConnected();
bool portalInfoShown = false;

unsigned long lastStatusDisplay = 0;
bool showingXboxStatus = false;
XboxPacket lastXboxPacket;


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
    tft.drawString("Type D setup", tft.width()/2, tft.height()/2);
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

    // ----------- PATCH START -----------
    // WiFiMgr replaces WiFiManager
    WiFiMgr::begin();
    Serial.println("[Type D] WiFiMgr initialized.");

    // If not connected, display portal info
    if (!WiFiMgr::isConnected()) {
        displayPortalInfo();
    }
    // ----------- PATCH END -------------

    // --- Start detection and web server modules ---
    Detect::begin();
    server8080.begin();
    FileMan::begin(server8080);
    cmd_init(&server8080, &tft);
    ESPNOWReceiver::begin();
    UI::begin(&tft);

    Serial.printf("[Type D] Device ID: %d\n", Detect::getId());

    // --- Show a random image on WiFi success as well ---
    ImageDisplay::displayRandomImage();

}

// Track WiFi connection state for debug and display logic
bool wasConnected = false;

void loop() {

    WiFiMgr::loop();
    // --- Highest priority: About animation ---
    if (ui_about_isActive()) {
        ui_about_update();
        return;
    }

    if (ui_bright_isVisible()) {
        ui_bright_update();
        return;
    }
    else if (UISet::isMenuVisible()) {
        UISet::update();
        return;
    }
    else {
        UI::update();
    }

    if (!UI::isMenuVisible()) {
        ImageDisplay::update();
    }

    bool nowConnected = WiFiMgr::isConnected();

    if (nowConnected && !wasConnected) {
        Serial.print("[Type D] ");
        Serial.println(WiFiMgr::getStatus());
        portalInfoShown = false;  // reset flag on connect
    }

    if (!nowConnected && wasConnected) {
        Serial.println("[Type D] WiFi connection lost, entering portal mode.");
        displayPortalInfo();
        portalInfoShown = true;
    }

    if (!nowConnected && !wasConnected) {
        if (!portalInfoShown) {
            displayPortalInfo();
            portalInfoShown = true;
        }
    }

    wasConnected = nowConnected;

    // --- Prevent image updates if not connected ---
    if (!nowConnected) return;

    // --- Handle Xbox status overlay ---
    if (ESPNOWReceiver::hasPacket()) {
        lastXboxPacket = ESPNOWReceiver::getLatest();
        xbox_status::show(&tft, lastXboxPacket);
        lastStatusDisplay = millis();
        showingXboxStatus = true;
    }

    if (showingXboxStatus) {
        if (millis() - lastStatusDisplay > 2000) {
            showingXboxStatus = false;
            ImageDisplay::displayRandomImage();
        }
        return;
    }

    ImageDisplay::update();
    cmd_serial_poll();
}