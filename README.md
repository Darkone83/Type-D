# Type D: Original Xbox TFT Display Project

<div align=center>
  <img src="https://github.com/Darkone83/Type-D/blob/main/images/logo.jpg">

  <img src="https://github.com/Darkone83/Type-D/blob/main/images/Types1.png"><img src="https://github.com/Darkone83/Type-D/blob/main/images/Types2.png">

  <img src="https://github.com/Darkone83/Type-D/blob/main/images/Duke1.jpg"><img src="https://github.com/Darkone83/Type-D/blob/main/images/Duke2.jpg">  
</div>

**Type D** is a two-part open-source system for the original Xbox, bringing a modern, customizable, and fully themed display experience to your console. It replaces the stock jewel with a 240x240 TFT touch display, controlled by an ESP32-S3, and optionally pairs with the Type D Expansion module for live Xbox status telemetry.

---

## Features

### Type D Display Firmware

- Animated boot screen from `/boot/boot.jpg` or `/boot/boot.gif`  
- Image & GIF slideshow: Will select a random image on boot and process through them
- Touchscreen Xbox-style menu:    
  - Adjust brightness
  - View current WiFi information    
  - Initiate WiFi setup or reset WiFi credentials  
- Web interface: Upload and manage gallery images over WiFi  

## Required Hardware

- **Type D Display**: ESP32 module (recommended: ESP32 + 240x240 TFT round GC9A01 display + CST816S touch panel + SD card support)

**AliExpress** <a href="https://www.aliexpress.us/item/3256805458117676.html?spm=a2g0o.order_list.order_list_main.5.38db18023gODs6&gatewayAdapt=glo2usa">ESP32-TFT 1.28 inch display</a>

**Amazon** <a href="https://www.amazon.com/dp/B0DLNMG2KP?ref=ppx_yo2ov_dt_b_fed_asin_title">1.28 inch ESP32-Development board</a>

- **Mounting Screws** 4 2x10 mm pan head screws (only required for the Type S installation)

- **Type S** Drill guide, and bezel plate (In the STL folder)

- **Duke** Cutting template, and spacer (In the STL folder)

## Purchasing

- **Full kit** Darkone Customs (Coming soon!)

- **Display with bezels** <a href="https://www.darkonecustoms.com/store/p/type-d-screen-kit">Darkone Customs (Type S)</a>

- **Expansion Module** <a href="https://www.darkonecustoms.com/store/p/type-d-telemetry-expansion">Darkone Customs</a>

- **Memcard PCB** <a href="https://www.darkonecustoms.com/store/p/type-d-memcard-pcb">Darkone Customs</a>. Installation instructions can be found <a href="https://github.com/Darkone83/Type-D/tree/main/pcb">here</a>.


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
2. **Install the ESP32 board package**
3. **Install all required libraries** (see above).
4. **Setup board and options** Board** ESP32 Dev module, Flash size: 16MB, Partition Scheme: 16MB Flash (2MB APP/12.5MB FATFS), PSRAM: Enabled
5. **Open the `Type_D.ino` project** and verify it compiles.
6. **Flash the firmware** to your module using USB.
7. **Connect to WiFi** Connect the device via wifi and select your network with the custom portal.
8. **Upload files** Log in to the File Manager via the web interface Http://"device-ip":8080 and upload your media. 
9. **Upload Resource files** Resource files can be uploaded via the resource manager Http://"device-ip":/8080/resources.


   **Notes:**
   - If no gallery images are present, a “No images found” screen will be shown.
   - File types supported are determined by firmware: common formats are `.jpg`,`.gif` (for UI assets).

## GIF Conversion

Use the script in the scripts folder to properly format your GIFS

Usage: gif_convert.py mygif.gif cool.gif

## Hardware installation

### Display Preparation

1 **Solder jumper** You will need to solder the jumper pad to the leg directly next to it to bypass the power button. See the Image below for reference.
<div align=center>

  ESP32
  
  <img src="https://github.com/Darkone83/Type-D/blob/main/images/Board%20prep/Jumper%20location.png">

  ESP32-S3
  
  <img src="https://github.com/Darkone83/Type-D/blob/main/images/Board%20prep/S3_PWR.JPG">
</div>

2 **Locate power**: Reference the image below for positive and negative terminals. Pre-tin the negative pad with a bit of flux and decent solder.
<div align=center>

  ESP32
  
  <img src="https://github.com/Darkone83/Type-D/blob/main/images/Board%20prep/Posneg.png">

  ESP32-S3
  
  <img src="https://github.com/Darkone83/Type-D/blob/main/images/Board%20prep/S3_PWR_POS_NEG.JPG">
</div>

3 ** Install power wires** Trim your leads as short as possible and solder your positive and negative conductors. Silicone sheathed wire is recommended for routing and flexibility.
<div align=center>
  
  Zoomed in ESP32:

  <img src="https://github.com/Darkone83/Type-D/blob/main/images/Board%20prep/Wire%20Zoom.png">

  Zoomed out ESP32:

  <img src="https://github.com/Darkone83/Type-D/blob/main/images/Board%20prep/Wire%20zoum%20out.png">

  Zoomed in ESP32-S3:

  <img src="https://github.com/Darkone83/Type-D/blob/main/images/Board%20prep/S3_PWR_WIRE.JPG">

### Type S:

**Notice** Beyond this point, you will be physically modifying your controller. This is not reversible, as you will have to drill holes in your shell. It's strongly advised that you use a shell that you're potentially alright with modifying.

**Required** You will need the <a href="https://github.com/Darkone83/Type-D/blob/main/stl/Type%20S%20Drill%20guide.stl">Type S drill guide</a> and the <a href="https://github.com/Darkone83/Type-D/blob/main/stl/Type%20S%20Bezel.stl">Type S Bezel</a>. You can asso use the optional <a href="https://github.com/Darkone83/Type-D/blob/main/stl/Washer.stl">washer</a> if needed.

<div align=center>
  <img src="https://github.com/Darkone83/Type-D/blob/main/images/Type%20S%20install/Type%20S%20Kit.png">
</div>

1 **Install Bezel** Peel the release paper from the screen and adhere it to the bezel
<div align=center>
  <img src="https://github.com/Darkone83/Type-D/blob/main/images/Board%20prep/Bezel.png">
</div>

2 **Remove jewel** Remove your jewel with a spudger or soft tool

<div align=center>
  Before:

  <img src="https://github.com/Darkone83/Type-D/blob/main/images/Type%20S%20install/Controller%20Front.png">

  After:

  <img src="https://github.com/Darkone83/Type-D/blob/main/images/Type%20S%20install/Controller%20Front%20No%20Jewel.png">
</div>

3 **Unscrew controller** Remove all screws shown in the image below
<div align=center>
  <img src="https://github.com/Darkone83/Type-D/blob/main/images/Type%20S%20install/Remove%20screws.png">
</div>

4 **Prep power points** Remove the PCB and prep your positive and negative points 
<div align=center>
  PCB Back:

  <img src="https://github.com/Darkone83/Type-D/blob/main/images/Type%20S%20install/PCB%20Back.png">

  Positive and Negative Points:

  <img src="https://github.com/Darkone83/Type-D/blob/main/images/Type%20S%20install/Posneg.png">
</div>

5 **Place Drill Guide** Take your time with this step and ensure you have good alignment
<div align=center>
  <img src="https://github.com/Darkone83/Type-D/blob/main/images/Type%20S%20install/Drill%20guide.png">
</div>

6 ** Drill holes** The point of no return, make sure you are 100% this is what you want
<div align=center>
  <img src="https://github.com/Darkone83/Type-D/blob/main/images/Type%20S%20install/Drilled.png">
</div>

7 **Secure Display** Install screws and secure the display to the controller
<div align=center>
  <img src="https://github.com/Darkone83/Type-D/blob/main/images/Type%20S%20install/Installed.png">
</div>

8 **Trim and solder conductors** Trim and solder your conductors to the correct positive and negative points. It's highly advised to test at this point
<div align=center>
  <img src="https://github.com/Darkone83/Type-D/blob/main/images/Type%20S%20install/Soldered.png">
</div>

9 **Reassembly** Reassemble your controller and enjoy!

### Duke:

1 **Additional Board Prep** Remove the 4 screw standoffs. This can be done with a soldering iron, but will be required if you don't want to trim the top memory card port.

<div align=center>
  <img src="https://github.com/Darkone83/Type-D/blob/main/images/Board%20prep/Post%20Remove.png">
</div>
2 **Disassemble the shell** Disassemble your Duke controller. Please note that the plastic is very brittle on these controllers; go slow and take your time.

<div align=center>
  <img src="https://github.com/Darkone83/Type-D/blob/main/images/Duke%20install/Screws.jpg">
</div>

3 **Front faceplate prep** Remove your front faceplate on the back; you will see a tab that sticks up, which will need to be trimmed, revealing the snap to the top jewel of the controller. Use a screwdriver to gently push the tab and remove the jewel from your controller.

<div align=center>
Front:

<img src="https://github.com/Darkone83/Type-D/blob/main/images/Duke%20install/Faceplate.jpg">

Area to cut first:

<img src="https://github.com/Darkone83/Type-D/blob/main/images/Duke%20install/Cut%20Tab.jpg">
</div>

4 **Cut the opening** Place the <a href="https://github.com/Darkone83/Type-D/blob/main/stl/Duke%20Guide.stl">cutting guide</a> where your jewel used to reside and mark your area to cut. For this install, I opted to go with a hot knife as the plastic is already extremely brittle. Go slow and take your time. Test for the display fitment along the way and trim as needed.

<div align=center>
  <img src="https://github.com/Darkone83/Type-D/blob/main/images/Duke%20install/Hole.jpg">
</div>

5 **Install spacer** Install your spacer with some super glue. The <a href="http://github.com/Darkone83/Type-D/blob/main/stl/Duke%20Spacer.stl">spacer</a> can be downloaded in the STL folder. Trim the shell as needed.

<div align=center>
  <img src="https://github.com/Darkone83/Type-D/blob/main/images/Duke%20install/Spacer.jpg">
</div>

6 **Install display** Fit your display by removing the release paper and adhering it in place. Take your time with alignment.

<div align=center>
  <img src="https://github.com/Darkone83/Type-D/blob/main/images/Duke%20install/Install.jpg">
</div>

7 **Locate power** Locate Positive and Negative on your controller. Reference the image below.

<div align=center>
  <img src="https://github.com/Darkone83/Type-D/blob/main/images/Duke%20install/Posneg.jpg">
</div>

8 ** Install power wires** Trim and install the conductors as seen below.

<div align=center>
  <img src="https://github.com/Darkone83/Type-D/blob/main/images/Duke%20install/Wire%20Install.jpg">
</div>

9 **Reassemble and enjoy** Reassemble your controller and enjoy!

## WiFi Connection:

Set up your WiFi by joining the Type D setup wifi network that Type D broadcasts. Join your preferred network and access the file manager to upload your content with the built-in file manager Http://"device ip":8080


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
- When a **Type D Expansion** module is active, the display instantly receives Xbox system telemetry and shows a themed overlay with the latest status—no configuration required. (Still in development)

---

## Diagnostics

You can access the diagnostic page once you have connected to wifi by visiting HTTP://"device IP":8080/diag

## Notes

- GIF support is experimental! Keep your GIF's under 1MB. Larger GIF's may work, but cause crashing of the firmware.

___

## Special Thanks

- Andr0, Team Resurgent, Cat-Pog-Real (STL Design), Xbox Scene, and the Xbox modding community

---

**Questions, feedback, or want to contribute? Open an issue or PR on GitHub!**
