# Type D ESP32-S3 Display Firmware

## Overview

**Type D** is a fully featured open-source firmware for an ESP32-S3-based display unit.  
It’s designed for rich UI, touch navigation, random/static image display, animated GIFs, a WiFi-configurable webserver, and full SD card image management.

- **Hardware:**  
  - ESP32-S3 (with PSRAM recommended)
  - 240x240 TFT display (TFT_eSPI compatible)
  - CST816S capacitive touch controller
  - SD Card (via SD_MMC)
  - BLK pin for backlight brightness (PWM via IO32)
- **Features:**  
  - JPG & GIF viewing (gallery, random, static modes)
  - Original Xbox-style menus, animated transitions
  - WiFiManager captive portal for network setup
  - Web upload of images/files via browser
  - Persistent settings (brightness, WiFi)
  - Firmware upgrade and diagnostics hooks

---

## Required Libraries

To compile and run the **Type D ESP32-S3 Display Firmware**, install these libraries in your Arduino IDE or PlatformIO environment:

### **Core Libraries (via Arduino Library Manager or GitHub)**

| Library            | Author/GitHub                      | Notes                          |
|--------------------|------------------------------------|-------------------------------|
| **TFT_eSPI**       | Bodmer                             | [GitHub link](https://github.com/Bodmer/TFT_eSPI) <br> For 240x240 TFT display (configure User_Setup.h for your wiring) |
| **TJpg_Decoder**   | Bodmer                             | [GitHub link](https://github.com/Bodmer/TJpg_Decoder) <br> JPEG image decoding |
| **AnimatedGIF**    | bitbank2                           | [GitHub link](https://github.com/bitbank2/AnimatedGIF) <br> GIF playback for ESP32 |
| **CST816S**        | fbiego                             | [GitHub link](https://github.com/fbiego/CST816S) <br> Touch controller driver |
| **WiFiManager**    | tzapu                              | [GitHub link](https://github.com/tzapu/WiFiManager) <br> For captive portal WiFi setup |
| **ESP32 FS**       | Espressif (built-in for ESP32)     | For SD_MMC and filesystem support |
| **Preferences**    | Espressif (built-in for ESP32)     | NVS (persistent storage) support |

**Other libraries (if needed for extras):**
- **WebServer** (built-in with ESP32 Arduino core)
- **SD_MMC** (built-in with ESP32 Arduino core)

### **How to Install**
1. Open Arduino IDE.
2. Go to `Sketch` → `Include Library` → `Manage Libraries…`
3. Search for and install each library above.
4. For libraries only available on GitHub (or for latest versions), use `Sketch` → `Include Library` → `Add .ZIP Library…` after downloading from GitHub.

**TFT_eSPI users:**  
Remember to configure your `User_Setup.h` or `User_Setups/SetupXX.h` for your wiring/pinout.

---

## File/Module Structure

| File/Folder           | Purpose / Key Functions                                 |
|-----------------------|--------------------------------------------------------|
| `Type_D.ino`          | Main application loop, hardware setup, core state      |
| `ui.cpp/.h`           | Menu/UI navigation, brightness, settings, About screen |
| `gallery.cpp/.h`      | Image gallery navigation and image selection logic     |
| `imagedisplay.cpp/.h` | Rendering engine for JPG/GIF, modes, timing            |
| `fileman.cpp/.h`      | SD card file browsing, image management                |
| `disp_cfg.h`          | Display and firmware config (rotation, version, etc)   |
| `fwupd.cpp/.h`        | (Optional) Firmware update handling                    |
| `detect.cpp/.h`       | (Optional) Device detection utilities                  |
| `/resources/`         | About screen images (DC.jpg, TR.jpg, XBS.jpg)          |
| `/boot/boot.jpg`      | Boot splash image (optional)                           |
| `/jpg/`               | User JPGs                                              |
| `/gif/`               | User GIFs                                              |

---

## Getting Started

### Hardware Connections

- **BLK (Backlight):** Connect display BLK to ESP32 IO32 for brightness control.
- **Touch:** SDA, SCL, RST, INT to the CST816S as per your pinout.
- **SD Card:** Wired to ESP32 SD_MMC interface.

### First Boot & Setup

1. Flash firmware to your ESP32-S3.
2. Insert SD card with `/jpg/` and/or `/gif/` folders (optional, can upload later).
3. Power up. The device will display a boot image (if present), then:
    - If WiFi is not configured, it launches the **WiFiManager captive portal** (“TypeD-Setup”)—connect to this SSID and set up WiFi.
    - If images are present, shows a random image. If none, prompts upload via web.
4. Access the device’s webserver (IP shown on display) to upload images.

---

## User Interface & Navigation

### Main UI States

- **Boot Splash:** Shows `/boot/boot.jpg` or `/boot/boot.gif` if present; otherwise shows “Type D Booting...”
- **Image Display:** Shows a random JPG or GIF.  
- **No Images:** If no images are found, displays a message with webserver IP.
- **WiFi Setup:** If WiFi not configured, shows instructions and captive portal info.
- **Menu:** Accessed by double-tapping the screen (CST816S double-tap gesture).

### Main Menu (double-tap to open)

- **Settings** (tap): Enter settings submenu.
- **Gallery** (tap): Browse JPGs/GIFs, pick display mode (see Gallery Menu).
- **About** (tap): See credits, version, and themed images (with fade transitions).
- **(Exit):** Double-tap to exit menu.

### Settings Submenu

- **Brightness:** Xbox-themed slider with `-` and `+` to set backlight (5–100%). Saved persistently.
- **Restart WiFi Portal:** Restarts captive portal (for new WiFi setup).
- **Forget WiFi:** Clears saved WiFi credentials (device will reboot to setup portal on next boot).
- **Back:** Returns to Main Menu.

### Gallery Menu

- **JPG:** Start gallery mode with first JPG from `/jpg/`.
- **GIF:** Start gallery mode with first GIF from `/gif/`.
- **Random JPG:** Random-slideshow from `/jpg/` (2s per image).
- **Random GIF:** Random-slideshow from `/gif/` (auto-advances after GIF ends).
- **Back:** Returns to Main Menu.

#### **Gallery Navigation Gestures**
- **Tap:** Selects/pauses current image.
- **Swipe Left/Right:** Next/previous image.
- **Swipe Up:** Exit to previous menu.

---

## Display Modes

- **Static:** Show selected JPG or loop GIF until user exits.
- **Random:** In random JPG/GIF mode, show new image (JPG = 2s, GIF = after last frame).
- **Gallery:** Navigate through images manually, or set one as “static” image.

---

## Web Interface

- **Access:** Find device IP on display (“Please upload images: x.x.x.x”).
- **Upload:** Drag-and-drop JPG/GIF files to `/jpg/` or `/gif/` via browser.

---

## Persistence

- **Brightness** and **WiFi** settings are stored in ESP32 NVS.
- WiFiManager handles WiFi configuration and can be reset via menu.

---

## Troubleshooting

- **No images found?**  
  - Upload via webserver, ensure correct folder structure.
- **No WiFi?**  
  - Hold for WiFiManager portal; restart from Settings if needed.
- **Display issues?**  
  - Check pin mapping in `disp_cfg.h` and wiring.
- **SD errors?**  
  - Format card FAT32, ensure folders are `/jpg/` and `/gif/`.

---

## Credits

- **Concept by:** Andr0
- **Code by:** Darkone83
---

## Customization

- Update splash/about images by replacing `/boot/boot.jpg` and `/resources/*.jpg`.
- Menu theming and transitions: edit `ui.cpp`.
- Add new features: extend menu or webserver as needed.

---

## License

*(Add your license here: MIT, GPL, etc)*

---
