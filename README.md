# Type D: Original Xbox TFT Display Project

**Type D** is a two-part open-source system for the original Xbox, bringing a modern, customizable, and fully themed display experience to your console. It replaces the stock jewel with a 240x240 TFT touch display, controlled by an ESP32-S3, and optionally pairs with the Type D Expansion module for live Xbox status telemetry.

---

## Features

### Type D Display Firmware

- Smart round TFT display for the Xbox jewel  
- Animated boot screen from `/boot/boot.jpg` or `/boot/boot.gif`  
- Image & GIF slideshow: Display static images or animated GIFs from SD card  
- Touchscreen Xbox-style menu:  
  - Navigate images  
  - Adjust brightness    
  - Initiate WiFi setup or reset WiFi credentials  
- Web interface: Upload and manage gallery images over WiFi  
- Themed UI: Custom-drawn menus and overlays with original Xbox flair 
- ESP-NOW Receiver:  

## Required Hardware

- **Type D Display**: ESP32-S3 module (recommended: ESP32-S3 + 240x240 TFT round GC9A01 display + CST816S touch panel + SD card support)


---

## Required Libraries

_Install these via Arduino Library Manager or from their GitHub releases:_

- [`LovyanGFX`](https://github.com/lovyan03/LovyanGFX)
- [`CST816S`](https://github.com/fbiego/CST816S) by fbiego
- [`AnimatedGIF`](https://github.com/bitbank2/AnimatedGIF)
- [`ESPAsyncWebserver`](https://github.com/me-no-dev/ESPAsyncWebServer)
- [`ESPAsyncTCP`](https://github.com/me-no-dev/ESPAsyncTCP)

---

## Build Instructions

### Type D Display 

#### Web Flasher: [![Type D Web Flasher](https://img.shields.io/badge/Web%20Flasher-Type%20D-green?logo=esp32&logoColor=white)](https://darkone83.github.io/type-d.github.io/)

#### Build Instructions
1. **Install Arduino IDE** (recommended 2.x or later).
2. **Install the ESP32 board package** via Board Manager, and select your ESP32-S3 board.
3. **Install all required libraries** (see above).
4. **Setup board and options** Board: ESP32 Dev module, Flash size: 16MB, Partition Scheme: 16MB Flash (2MB APP/12.5MB FATFS), PSRAM: Enabled
5. **Open the `Type_D.ino` project** and verify it compiles.
6. **Flash the firmware** to your module using USB.
7. **Connect to WiFi** Connect the device via wifi and select your network with the custom portal.
8. **Upload files**: Log in to the File Manager via the web interface Http://<device-ip>:8080 and upload your media. 


   **Notes:**
   - If no gallery images are present, a “No images found” screen will be shown.
   - File types supported are determined by firmware: common formats are `.jpg`,`.gif` (for UI assets).
   - Resource files should use names expected by your UI/menu code. you can access the resource uploader bu going to device=ip:8080/resource
   - Large image files may affect load speed; optimize/resize for best results.

## Hardware installation

### Type D display
TODO



## 🗺️ Navigation & Menu Tree

**Main Menu Structure:**
```
[ Main Menu ]
 ├─ Settings
 │    ├─ Brightness
 │    └─ WiFi info
 │    └─ Forget WiFi
 │    └─ Back
 ├─ About
 └─ Exit
```

### Navigation

- **Touch**
  - *Single tap*: Select menu item
  - *Swipe up/down*: Scroll through menus or items
  - *Double tap*: Enter Type D Menu
- **Settings Menu**
  - *Brightness*: Adjust display backlight (0-100%)
  - *WiFi Info*: Displays current WiFi network connection and IP address
  - *Forget WiFi*: Forgets WiFi settings and restarts the connection portal
  - *Back*: Returns to main menu
- **About**: Shows build/version info and credits
- **Exit**: Exits menu
---

## How It Works

- **Type D Display** runs as a stand-alone smart dashboard and animated photo frame, displaying images and menus when no Xbox is online.
- When a **Type D Expansion** module is active on the same network, the display instantly receives Xbox system telemetry and shows a themed overlay with the latest status—no configuration required.

---

## Notes

- GIF support is experimental at this point and can cause issues or crashes It's being looked into.

___

## Special Thanks

- Andr0, Team Resurgent, Xbox Scene, and the Xbox modding community

---

**Questions, feedback, or want to contribute? Open an issue or PR on GitHub!**
