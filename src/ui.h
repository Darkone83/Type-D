// ui.h
#ifndef UI_H
#define UI_H

#include <Arduino.h>
#include "CST816S.h"
#include "disp_cfg.h"  // For I2C and display definitions

namespace UI {
    void begin(LGFX* tft);
    void update();
    bool isMenuVisible();
    void showMenu();
    void drawMenu();
}

#endif
