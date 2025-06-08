# Type D & Type D Expansion Firmware

![Web Flasher](https://img.shields.io/badge/Web%20Flasher-Type%20D-green?style=for-the-badge&logo=xbox)
[Flash with Web Flasher](https://darkone83.github.io/type-d.github.io/)

## Overview

**Type D** is an all-in-one display and control firmware for ESP32-based Xbox mod and telemetry projects. It provides an Xbox-themed UI, dynamic gallery, touch controls, network configuration, and more.  
**Type D Expansion** is the companion firmware for ESP32 telemetry transmitters, enabling real-time system monitoring via ESP-NOW.

---

## Features

### Type D (Receiver/Display Firmware)
- Xbox-inspired touch UI for 240x240 round and square displays (TFT_eSPI compatible)
- Auto image and GIF gallery with SD card support
- ESP-NOW receiver for real-time Xbox telemetry (CPU temp, ambient, fan, current app/title)
- On-device WiFi configuration via WiFiManager (120s timeout, web portal)
- OTA firmware upgrade and web command portal
- Settings menu: brightness, restart/forget WiFi, display sleep, gallery browser
- Brightness adjustment with PWM backlight control and persistent storage
- About screens with custom graphics
- Gallery mode with swipe browsing of SD card images/GIFs
- Touch gestures: tap, double-tap, swipe (menu navigation)
- Persistent settings for brightness, display sleep, and WiFi
- Robust fallback: text boot if SD card/graphics missing

### Type D Expansion (Transmitter/Telemetry Firmware)
- Collects Xbox hardware status via SMBus (or similar interface)
- Transmits CPU temp, ambient temp, fan speed, and running app/title over ESP-NOW
- Optimized packet format for minimal latency
- Automatically detects and connects to Type D receivers
- Designed for seamless integration in original Xbox consoles or mods
- Configurable transmission intervals and pairing
- Minimal resource footprint; ready for expansion

---

## Navigation Instructions

- **Main Menu**
  - Tap items to select.
  - Double-tap to exit menu.
- **Settings**
  - Adjust brightness with slider or plus/minus buttons.
  - Restart WiFi portal, forget network, toggle display sleep, and view About.
  - Use swipe up/down for vertical scrolling if needed.
- **Gallery**
  - Browse images/GIFs, tap to view.
  - Swipe for next/previous images.
- **Display Sleep**
  - Toggle enable/disable.
  - Tap plus/minus to set timeout.
  - Back to previous menu.
- **Waking Display**
  - Single tap at any time wakes the display from sleep.

---

## Menu Additions

The UI has been expanded with the following features and menu items:

### New Menu Items

- **Settings Menu:**
  - **Display Sleep**
    - Enable or disable automatic display sleep.
    - Adjustable sleep timeout: 1 min, 2 mins, 5 mins, 10 mins.
    - Settings persist through reboots.
    - Tap plus (+) or minus (–) to adjust the timeout value.
    - Tap on “Sleep: Enabled/Disabled” to toggle the state. Enabled is shown in white/green; Disabled in red.
    - “Back” option returns to the previous menu.
  - **Gallery**
    - Browse image and GIF gallery stored on SD card.
    - Gallery supports swipe navigation and touch selection (if enabled).
- **Scrolling Support:**  
  - If menu items exceed screen height, vertical scrolling is enabled with simple drag gestures up/down.
  - Long menu item names are handled with reduced font size or adaptive wrapping to prevent text cutoff.

### Persistent Settings

- Display sleep state and timeout are stored in flash and restored on boot.

### Interaction Summary

- **Touch**:  
  - Tap plus/minus to adjust values.  
  - Tap options to select or toggle.  
  - Single tap wakes display if sleeping.
- **Visual feedback**:  
  - Xbox-themed highlights and color cues for active/inactive states.

---

## Required Modules/Libraries

- Arduino ESP32 core (v3.x recommended)
- [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI)
- [WiFiManager](https://github.com/tzapu/WiFiManager)
- [Preferences](https://github.com/espressif/arduino-esp32/tree/master/libraries/Preferences)
- [SD_MMC](https://github.com/espressif/arduino-esp32/tree/master/libraries/SD_MMC)
- [TJpg_Decoder](https://github.com/Bodmer/TJpg_Decoder)
- [AnimatedGIF](https://github.com/bitbank2/AnimatedGIF)
- [CST816S](https://github.com/Darkone83/CST816S) (or compatible for touch panel)
- (No `User_Setup.h` required – custom config used)

---

## Build Instructions

1. Clone the repo and open the `Type_D` folder in Arduino IDE or VSCode with PlatformIO.
2. Select your ESP32 board and set proper serial port.
3. Install the libraries listed above using Arduino Library Manager or PlatformIO.
4. Configure your display and pin assignments in `disp_cfg.h`.
5. Connect the hardware:
    - Display (SPI)
    - Touch panel (I2C)
    - Backlight (GPIO)
    - SD card (SD_MMC)
6. Flash the firmware using the Arduino IDE, PlatformIO, or the [Web Flasher](https://darkone83.github.io/type-d.github.io/).
7. Insert a properly structured SD card.

---

## SD Card Setup

1. Format SD card (FAT32 recommended).
2. **Root Folder Structure:**
/ (root)
├── boot/ # Boot screen images (boot.jpg, boot.gif)
├── jpg/ # All JPGs go here
├── gif/ # All GIFs go here
├── resources/ # UI resources
├── update/ # Place upgrade.bin files here for SD update

3. **jpg/, gif/**  
- Place all .jpg in the jpg/ folder and all .gif in the gif/ folder.

4. **boot/**  
- Optional boot animation (boot.jpg or boot.gif will be shown at startup if present).

5. **resources/**  
- UI images/icons:  
  - `cpu.jpg` – CPU temp icon  
  - `amb.jpg` – Ambient temp icon  
  - `fan.jpg` – Fan speed icon  
  - `app.jpg` – Application/title icon  
  - `DC.jpg`, `TR.jpg`, `XBS.jpg` for About screen and branding

6. **update/**  
- Place OTA upgrade`.bin` files for SD update.

---

## License

[MIT License](LICENSE)

---

## Credits

- **Concept:** Andr0  
- **Code:** Darkone83  
- Thanks to Team Resurgent, XBOX Scene, and the modding community

---

