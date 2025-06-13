#ifndef UI_WINFO_H
#define UI_WINFO_H

void ui_winfo_open();          // Open the WiFi Info menu
void ui_winfo_exit();          // Exit the WiFi Info menu (returns to settings)
bool ui_winfo_isVisible();     // Is the WiFi Info menu currently visible?
void ui_winfo_update();        // Menu update/handler

#endif // UI_WINFO_H
