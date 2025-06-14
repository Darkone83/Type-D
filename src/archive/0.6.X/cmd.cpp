#include "cmd.h"
#include "disp_cfg.h"
#include <Arduino.h>
#include "imagedisplay.h"
#include "wifimgr.h"
// Include your brightness module as needed


static LGFX* s_tft = nullptr;

enum {
    CMD_NEXT_IMAGE      = 0x01,
    CMD_PREV_IMAGE      = 0x02,
    CMD_RANDOM_IMAGE    = 0x03,
    CMD_DISPLAY_MODE    = 0x04,
    CMD_DISPLAY_IMAGE   = 0x05,
    CMD_DISPLAY_CLEAR   = 0x06,

    CMD_BRIGHTNESS_SET  = 0x20,

    CMD_WIFI_RESTART    = 0x30,
    CMD_WIFI_FORGET     = 0x31,

    CMD_REBOOT          = 0x40,

    CMD_DISPLAY_ON      = 0x60,
    CMD_DISPLAY_OFF     = 0x61,
};

static void execute_cmd(uint8_t code, AsyncWebServerRequest* request = nullptr, Stream* serial = nullptr);

static void handle_cmd(AsyncWebServerRequest *request) {
    if (!request->hasParam("c")) {
        request->send(400, "application/json", "{\"err\":\"Missing command param\"}");
        return;
    }
    String cstr = request->getParam("c")->value();
    uint8_t code = (uint8_t)strtol(cstr.c_str(), nullptr, 16);
    execute_cmd(code, request, nullptr);
}

void cmd_init(AsyncWebServer *server, LGFX *tft) {
    s_tft = tft;
    server->on("/cmd", HTTP_GET, handle_cmd);
    Serial.println("[cmd] /cmd HTTP endpoint registered");
}

static String serial_line;
void cmd_serial_poll() {
    while (Serial.available()) {
        char ch = Serial.read();
        if (ch == '\n' || ch == '\r') {
            if (serial_line.length() > 0) {
                int code = -1;
                int val = -1;
                char param_key[16] = {0};
                char param_val_str[32] = {0};
                String param_file = "";
                String param_mode = "";
                if (sscanf(serial_line.c_str(), "c=%x&val=%d", &code, &val) == 2) {
                    execute_cmd((uint8_t)code, nullptr, &Serial);
                } else if (sscanf(serial_line.c_str(), "c=%x&file=%31s", &code, param_val_str) == 2) {
                    param_file = String(param_val_str);
                    execute_cmd((uint8_t)code, nullptr, &Serial);
                } else if (sscanf(serial_line.c_str(), "c=%x&mode=%31s", &code, param_val_str) == 2) {
                    param_mode = String(param_val_str);
                    execute_cmd((uint8_t)code, nullptr, &Serial);
                } else if (sscanf(serial_line.c_str(), "c=%x", &code) == 1) {
                    execute_cmd((uint8_t)code, nullptr, &Serial);
                } else {
                    Serial.printf("[cmd] Invalid serial command: %s\n", serial_line.c_str());
                }
            }
            serial_line = "";
        } else {
            serial_line += ch;
            if (serial_line.length() > 128) serial_line = "";
        }
    }
}

static void execute_cmd(uint8_t code, AsyncWebServerRequest* request, Stream* serial) {
    int val = -1;
    String param_file, param_mode;
    if (request) {
        if (request->hasParam("val")) val = request->getParam("val")->value().toInt();
        if (request->hasParam("file")) param_file = request->getParam("file")->value();
        if (request->hasParam("mode")) param_mode = request->getParam("mode")->value();
    }

    Serial.printf("[cmd] Executing code 0x%02X", code);
    if (val != -1) Serial.printf(" val=%d", val);
    if (param_file.length()) Serial.printf(" file=%s", param_file.c_str());
    if (param_mode.length()) Serial.printf(" mode=%s", param_mode.c_str());
    Serial.println();

    switch (code) {
        case CMD_NEXT_IMAGE:
            ImageDisplay::nextImage();
            break;
        case CMD_PREV_IMAGE:
            ImageDisplay::prevImage();
            break;
        case CMD_RANDOM_IMAGE:
            ImageDisplay::displayRandomImage();
            break;
        case CMD_DISPLAY_MODE:
            if (param_mode == "jpg" || val == 0) ImageDisplay::setMode(ImageDisplay::MODE_JPG);
            else if (param_mode == "gif" || val == 1) ImageDisplay::setMode(ImageDisplay::MODE_GIF);
            else ImageDisplay::setMode(ImageDisplay::MODE_RANDOM);
            break;
        case CMD_DISPLAY_IMAGE:
            if (param_file.length()) ImageDisplay::displayImage(param_file);
            break;
        case CMD_DISPLAY_CLEAR:
            ImageDisplay::clear();
            break;
        case CMD_BRIGHTNESS_SET:
            if (val >= 5 && val <= 100) {
                // TODO: call your brightness set function here!
                Serial.printf("[cmd] Would set brightness to %d\n", val);
            }
            break;
        case CMD_WIFI_RESTART:
            WiFiMgr::restartPortal();
            break;
        case CMD_WIFI_FORGET:
            WiFiMgr::forgetWiFi();
            break;
        case CMD_REBOOT:
            ESP.restart();
            break;
        case CMD_DISPLAY_ON:
            if (s_tft) s_tft->powerSave(false);
            break;
        case CMD_DISPLAY_OFF:
            if (s_tft) s_tft->powerSave(true);
            break;
        default:
            Serial.printf("[cmd] Unknown code 0x%02X\n", code);
            break;
    }

    if (request) request->send(200, "application/json", "{\"ok\":1}");
}
