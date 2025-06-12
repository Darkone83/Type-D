#ifndef FILEMAN_H
#define FILEMAN_H

#include <Arduino.h>
class WebServer;

namespace FileMan {
    void begin(WebServer& server);
}

#endif
