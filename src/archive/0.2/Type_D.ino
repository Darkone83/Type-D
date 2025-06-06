#include <FS.h>
#include <WebServer.h>
#include <TFT_eSPI.h>
#include <SD_MMC.h>
#include <WiFiManager.h>
#include <CST816S.h>
#include <TJpg_Decoder.h>
#include <AnimatedGIF.h>
using fs::FS;

#include "disp_cfg.h"
#include "detect.h"
#include "fwupd.h"
#include "imagedisplay.h"
#include "ui.h"
#include "fileman.h"
#include "cmd.h"

// --- Globals ---
TFT_eSPI tft = TFT_eSPI();
CST816S touch(TOUCH_SDA, TOUCH_SCL, TOUCH_RST, TOUCH_INT);
WiFiManager wm;
AnimatedGIF gif;
WebServer server(8080);

extern String getRandomGalleryImagePath(); // From fileman.cpp

bool wifiSetupInProgress = false;

// Required by TJpgDec
bool tft_jpg_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap) {
    tft.pushImage(x, y, w, h, bitmap);
    return true;
}

// --- UI State Management ---
enum UiState { UI_BOOT, UI_IMAGE, UI_MENU, UI_NO_IMAGES, UI_WIFI_SETUP, UI_BRIGHTNESS };
UiState uiState = UI_BOOT;
unsigned long bootStartTime = 0;
const unsigned long BOOT_DISPLAY_DURATION = 2000;

// --- Prototypes ---
void showRandomImage();
void showNoImagesFound(const String& ipAddr);
void showWiFiManagerInfo();
void restartWiFiManager();
bool showBootScreen();

void setup() {
    Serial.begin(115200);
    delay(200);

    tft.begin();
    tft.setRotation(DISP_ROTATION);
    tft.fillScreen(TFT_BLACK);

    UI::begin(&tft);

    if (!SD_MMC.begin()) {
        Serial.println("SD_MMC Mount Failed!");
    }
    touch.begin();

    TJpgDec.setJpgScale(1);
    TJpgDec.setCallback(tft_jpg_output);

    // --- Firmware Update on Boot ---
    FWUpd::checkAndUpgrade();

    // --- WiFiManager (120s timeout) ---
    wm.setConfigPortalTimeout(120);

    // --- Start WebServer and register FileMan routes ---
    server.begin();
    FileMan::begin(server);

    server.on("/cmd", HTTP_GET, []() {
    String codeStr = server.arg("code");
    String arg = server.hasArg("arg") ? server.arg("arg") : "";
    if (codeStr == "help") {
        server.send(200, "text/plain", Cmd::help());
        return;
    }
    if (codeStr.length() < 4) {
        server.send(400, "text/plain", "Missing/invalid code");
        return;
    }
    uint16_t code = strtol(codeStr.c_str(), nullptr, 16);
    String result = Cmd::execute(code, arg);
    server.send(200, "text/plain", result);
    });

    bootStartTime = millis();
    bool bootShown = showBootScreen();
    if (!bootShown) {
        tft.fillScreen(TFT_BLACK);
        tft.setTextDatum(MC_DATUM);
        tft.setTextFont(4);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.drawString("Type D", tft.width() / 2, tft.height() / 2 - 24);
        tft.setTextFont(2);
        tft.drawString("Booting.....", tft.width() / 2, tft.height() / 2 + 16);
    }
}

void loop() {
    // --- Handle touch events ---
    if (touch.available()) {
        // Enter menu from image/no-image/wifi-setup
        if ((uiState == UI_IMAGE || uiState == UI_NO_IMAGES || uiState == UI_WIFI_SETUP) && touch.data.gestureID == DOUBLE_CLICK) {
            uiState = UI_MENU;
            UI::showMenu();
        }
        // Pass gestures to UI in menu mode
        if (uiState == UI_MENU) {
            UI::loop(touch.data.gestureID);
        }
    }

    // --- Handle touch events ---
    if (touch.available()) {
        // Enter menu from image/no-image/wifi-setup
        if ((uiState == UI_IMAGE || uiState == UI_NO_IMAGES || uiState == UI_WIFI_SETUP) && touch.data.gestureID == DOUBLE_CLICK) {
            uiState = UI_MENU;
            UI::showMenu();
        }
        // Main menu handling
        if (uiState == UI_MENU) {
            // Save old menu state
            bool wasMenu = UI::menuVisible();
            UI::loop(touch.data.gestureID);
            // Detect if brightness menu was activated
            if (!wasMenu && UI::brightnessMenuActive()) {
                uiState = UI_BRIGHTNESS;
            }
        }
        // Brightness menu handling
        if (uiState == UI_BRIGHTNESS && touch.data.gestureID == 5) { // 5 = tap
            UI::brightnessMenuLoop(touch.data.x, touch.data.y, true);
        }
    }

    switch (uiState) {
        case UI_BOOT: {
            if (millis() - bootStartTime > BOOT_DISPLAY_DURATION) {
                String imgPath = getRandomGalleryImagePath();
                if (imgPath.length() > 0) {
                    uiState = UI_IMAGE;
                    ImageDisplay::displayImage(imgPath);
                } else {
                    // No images found; check if connected
                    if (!WiFi.isConnected()) {
                        if (!wifiSetupInProgress) {
                            wifiSetupInProgress = true;
                            uiState = UI_WIFI_SETUP;
                            wm.startConfigPortal("TypeD-Setup");
                            showWiFiManagerInfo();
                        }
                    } else {
                        String ipAddr = WiFi.localIP().toString();
                        uiState = UI_NO_IMAGES;
                        showNoImagesFound(ipAddr);
                    }
                }
            }
            break;
        }
        case UI_WIFI_SETUP:
            showWiFiManagerInfo();
            // Check for WiFi connection
            if (WiFi.isConnected()) {
                wifiSetupInProgress = false;
                String imgPath = getRandomGalleryImagePath();
                if (imgPath.length() > 0) {
                    uiState = UI_IMAGE;
                    ImageDisplay::displayImage(imgPath);
                } else {
                    uiState = UI_NO_IMAGES;
                    showNoImagesFound(WiFi.localIP().toString());
                }
            } else if (!wm.getConfigPortalActive()) {
                showWiFiManagerInfo();
            }
            break;

        case UI_IMAGE:
            // Image is displayed; implement slideshow or transitions as desired
            break;

        case UI_MENU:
            // UI handles menu logic; check if menu should exit
            if (UI::shouldExitMenu()) {
                UI::resetMenuExit();
                String imgPath = getRandomGalleryImagePath();
                if (imgPath.length() > 0) {
                    uiState = UI_IMAGE;
                    ImageDisplay::displayImage(imgPath);
                } else {
                    uiState = UI_NO_IMAGES;
                    showNoImagesFound(WiFi.localIP().toString());
                }
            }
            break;

        case UI_NO_IMAGES:
            // Show no images; can trigger WiFiManager again from UI/menu if you wish
            break;

        case UI_BRIGHTNESS:
            // All touch/tap logic handled by UI::brightnessMenuLoop()
            // When you add a "Back" button, you will set uiState = UI_MENU or whatever is appropriate
            break;

    }
}

// --- Utility Functions ---
void showRandomImage() {
    String imgPath = getRandomGalleryImagePath();
    if (imgPath.length() == 0) {
        String ipAddr;
        if (WiFi.isConnected())
            ipAddr = WiFi.localIP().toString();
        else
            ipAddr = WiFi.softAPIP().toString();
        if (ipAddr.length() > 0)
            showNoImagesFound(ipAddr);
        else {
            uiState = UI_WIFI_SETUP;
            wifiSetupInProgress = true;
            wm.startConfigPortal("TypeD-Setup");
            showWiFiManagerInfo();
        }
        return;
    }
    ImageDisplay::displayImage(imgPath);
}

void showNoImagesFound(const String& ipAddr) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(4);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("No images found", tft.width()/2, tft.height()/2 - 32);
    tft.setTextFont(2);
    tft.drawString(ipAddr, tft.width()/2, tft.height()/2);
    tft.drawString("Please upload images", tft.width()/2, tft.height()/2 + 32);
}

void showWiFiManagerInfo() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(4);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("WiFi Setup", tft.width()/2, tft.height()/2 - 34);
    tft.setTextFont(2);
    tft.drawString("SSID: TypeD-Setup", tft.width()/2, tft.height()/2);
    tft.drawString("IP: " + WiFi.softAPIP().toString(), tft.width()/2, tft.height()/2 + 20);
    tft.drawString("Connect & upload images", tft.width()/2, tft.height()/2 + 44);
}

void restartWiFiManager() {
    wm.setConfigPortalTimeout(120);
    wifiSetupInProgress = true;
    uiState = UI_WIFI_SETUP;
    wm.startConfigPortal("TypeD-Setup");
    showWiFiManagerInfo();
}

bool showBootScreen() {
    if (SD_MMC.exists("/boot/boot.jpg")) {
        uint16_t jpgWidth, jpgHeight;
        if (TJpgDec.getJpgSize(&jpgWidth, &jpgHeight, "/boot/boot.jpg") == 0) {
            int x = (TFT_WIDTH - jpgWidth) / 2;
            int y = (TFT_HEIGHT - jpgHeight) / 2;
            TJpgDec.drawSdJpg(x, y, "/boot/boot.jpg");
            return true;
        }
    }
    if (SD_MMC.exists("/boot/boot.gif")) {
        if (gif.open("/boot/boot.gif",
            ImageDisplay::GIFOpenFile,
            ImageDisplay::GIFCloseFile,
            ImageDisplay::GIFReadFile,
            ImageDisplay::GIFSeekFile,
            ImageDisplay::gifDraw)) {
            ImageDisplay::gifBootActive = true;
            return true;
        }
    }
    return false;
}
