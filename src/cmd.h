#pragma once

#include <Arduino.h>

namespace Cmd {

// Command codes
enum : uint16_t {
    CMD_RESTART_WIFI   = 0x0102,
    CMD_FORGET_WIFI    = 0x0103,
    CMD_SET_BRIGHTNESS = 0x0101,
    CMD_ABOUT          = 0x0300,
    CMD_NEXT_IMAGE     = 0x0210,
    CMD_PREV_IMAGE     = 0x0211,
    CMD_RANDOM_JPG     = 0x0203,
    CMD_RANDOM_GIF     = 0x0204,
    CMD_REBOOT         = 0x0FFF,
};

String execute(uint16_t code, const String& arg = "");
String help();
}
