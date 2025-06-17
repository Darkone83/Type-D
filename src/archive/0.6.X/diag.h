// diag.h
#pragma once
#include <ESPAsyncWebServer.h>

namespace Diag {
    void begin(AsyncWebServer &server);
    void handle();
}
