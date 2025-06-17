#ifndef UI_BRIGHT_H
#define UI_BRIGHT_H

#include <Arduino.h>
#include "CST816S.h"

void ui_bright_open();
void ui_bright_exit();
bool ui_bright_isVisible();
void ui_bright_update();
void ui_bright_applySaved();

#endif
