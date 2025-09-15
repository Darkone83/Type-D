// ui.h
#ifndef UI_H
#define UI_H

#include <Arduino.h>
#include "CST816S.h"
#include "disp_cfg.h"

namespace UI {
    void begin(LGFX* tft);
    void update();
    bool isMenuVisible();
    void showMenu();
    void drawMenu();
}

#endif
