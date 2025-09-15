// diag.h
#pragma once
#include <ESPAsyncWebServer.h>

// Declare the OTA-in-progress flag for use in all source files
extern volatile bool g_otaInProgress;

namespace Diag {
    void begin(AsyncWebServer &server);
    void handle();
}
