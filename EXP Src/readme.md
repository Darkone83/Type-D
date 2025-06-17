# Type D EXP

Firmware for the Waveshare ESP32-S3-Zero, featuring UDP-based status reporting.

## Required Hardware

Waveshare ESP32-S3 Zero

<a href="https://www.amazon.com/dp/B0CS6VS1DJ?ref_=ppx_hzsearch_conn_dt_b_fed_asin_title_2">Amazon</a>

## Build Instructions (Arduino IDE)

1. **Board Setup**  
   - Open Arduino IDE.  
   - Go to **Tools → Board → ESP32 Arduino → ESP32S3 Dev Module**.  
   - Select your **Port** under **Tools → Port**.

2. **Project Files**  
   - Copy all project files (including `Type_D_EXP.ino` and related `.cpp`/`.h` files) into your Arduino sketch folder.

3. **Install Libraries**  
   - AsyncTCP  
   - ESPAsyncWebServer  


5. **Build and Upload**  
   - Open `Type_D_EXP.ino` in Arduino IDE.  
   - Click **Upload**.

## Installation

Solder SDA from the LCP header to Pin 7, SCL from the LPC to pin 6, GND to GNV, and 5v from the LPV to 5v on the S3

Mount to the RF shield with VHB tape or foam tape

## Connect to your wifi network

Locate and join the Type D EXP Setup network, and join your preferred wifi network. Both Type D and Type D EXP need to be on the same network to work properly.

## LED Statuses

- White: Booting
- Blinking Purple: WiFi portal active
- Green: connected to WiFi
- Red: Wifi connection failed
- Orange blink: UPD Packet send *not fully working*

