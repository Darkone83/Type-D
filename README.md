# Type D: Original Xbox TFT Display Project

**Type D** is a two-part open-source system for the original Xbox, bringing a modern, customizable, and fully themed display experience to your console. It replaces the stock jewel with a 240x240 TFT touch display, controlled by an ESP32-S3, and optionally pairs with the Type D Expansion module for live Xbox status telemetry.

---

## Features

### Type D Display Firmware (ESP32-S3)

- Smart round TFT display for the Xbox jewel  
- Animated boot screen from `/boot/boot.jpg` or `/boot/boot.gif`  
- Image & GIF slideshow: Display static images or animated GIFs from SD card  
- Touchscreen Xbox-style menu:  
  - Navigate images  
  - Adjust brightness  
  - View device/firmware info  
  - Initiate WiFi setup or reset WiFi credentials  
- WiFiManager captive portal: Simple WiFi setup with a custom splash page  
- Web interface: Upload and manage gallery images over WiFi  
- Persistent settings: Stores WiFi and brightness settings in flash  
- Themed UI: Custom-drawn menus and overlays with original Xbox design language  
- ESP-NOW Receiver:  
  - **Displays live Xbox system status when paired with the Type D Expansion module**  
  - Shows real-time overlay with fan speed, CPU temp, network IP, and current title/app  

### Type D Expansion Firmware (ESP32/ESP8266)

- Plugs into your Xbox (or companion mod board)  
- Gathers system telemetry:  
  - Fan speed  
  - CPU temperature  
  - Current IP address  
  - Currently running game/title/app  
- Broadcasts telemetry via ESP-NOW (no WiFi setup needed)  
- Instant pairing with any nearby Type D Display unit  
- Low-latency, infrastructure-free wireless communication  
- Easy to integrate with homebrew, modchips, or expansion boards  

---

## Required Hardware

- **Type D Display**: ESP32-S3 module (recommended: ESP32-S3 + 240x240 TFT round GC9A01 display + CST816S touch panel + SD card support)
- **Type D Expansion**: ESP32 or ESP8266 module (for Xbox telemetry sender)

---

## Required Libraries

_Install these via Arduino Library Manager or from their GitHub releases:_

- [`TFT_eSPI`](https://github.com/Bodmer/TFT_eSPI)
- [`WiFiManager`](https://github.com/tzapu/WiFiManager)
- [`CST816S`](https://github.com/fbiego/CST816S) by fbiego
- [`TJpg_Decoder`](https://github.com/Bodmer/TJpg_Decoder)
- [`AnimatedGIF`](https://github.com/bitbank2/AnimatedGIF)
---

## Build Instructions

### Type D Display (ESP32-S3)

1. **Install Arduino IDE** (recommended 2.x or later).
2. **Install the ESP32 board package** via Board Manager, and select your ESP32-S3 board.
3. **Install all required libraries** (see above).
4. **No need to modify `User_Setup.h`**:  
   - The project uses a custom display configuration file (`disp_cfg.h`).
5. **Open the `Type_D.ino` project** and verify it compiles.
6. **Flash the firmware** to your ESP32-S3 using USB. Or use the Web flasher [![Type D Web Flasher](https://img.shields.io/badge/Web%20Flasher-Type%20D-green?logo=esp32&logoColor=white)](https://darkone83.github.io/type-d.github.io/)

7. **SD Card Setup:**  
   - Format your SD card as **FAT32** and arrange the folders and files as follows:


   - **/boot/**: Boot splash image or animation displayed at startup.
   - **/gallery/**: Images and GIFs shown in the main gallery or slideshow.
   - **/resources/**: All UI assets (icons, overlays, fonts) loaded dynamically by the firmware.
   - **/update/**: Place a `upgrade.bin` here to perform an SD-based firmware upgrade.

   **Notes:**
   - If no gallery images are present, a “No images found” screen will be shown.
   - File types supported are determined by firmware: common formats are `.jpg`, `.gif` (for UI assets).
   - Resource files should use names expected by your UI/menu code.
   - Only one firmware file should be present in `/update/` at a time.
   - Large image files may affect load speed; optimize/resize for best results.



### Type D Expansion (ESP32-S3)

1. **Install Arduino IDE** and ESP32/ESP8266 board package as appropriate.
2. **Open the `Type_D_exp.ino` (or equivalent)** and configure your hardware pins and Xbox data sources.
3. **Flash the firmware** to the ESP32/ESP8266 module.
4. **Wire the module** to the Xbox 5V, GND, SDA and SCL
---

## Hardware installation

### Type D display
TODO

### Type D Expansion
TODO

## Navigation Instructions

### On-Screen Touch Controls:

- **Double-tap** anywhere on the display to open the main menu.
- **Swipe left/right** to browse through gallery images (if enabled).
- **Tap menu icons** to:
  - Adjust screen brightness
  - View device info/about
  - Start or reset WiFi setup
  - Return to the main gallery view
- **To exit menus**, select the “Back” or exit option, or double-tap again (as indicated).
- When Xbox telemetry is active, a status overlay will appear automatically and disappear after a few seconds—no interaction needed.

---

## How It Works

- **Type D Display** runs as a stand-alone smart dashboard and animated photo frame, displaying images and menus when no Xbox is online.
- When a **Type D Expansion** module is active on the same network, the display instantly receives Xbox system telemetry and shows a themed overlay with the latest status—no configuration required.

---

## Special Thanks

- Andr0, Team Resurgent, Xbox Scene, and the xbox modding community

---

**Questions, feedback, or want to contribute? Open an issue or PR on GitHub!**
