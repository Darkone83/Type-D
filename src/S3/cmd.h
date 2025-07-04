#pragma once
#include <Arduino.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

// Forward declare for LGFX pointer
class LGFX;

// Call this in setup after LGFX and server are initialized
void cmd_init(AsyncWebServer *server, LGFX *tft);

// Call in loop() to poll for serial commands
void cmd_serial_poll();

