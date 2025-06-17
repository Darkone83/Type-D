# Type_D_EXP

Firmware for the Waveshare ESP32-S3-Zero, featuring a touch display interface and UDP-based status reporting.

## Build Instructions (Arduino IDE)

1. **Board Setup**  
   - Open Arduino IDE.  
   - Go to **Tools → Board → ESP32 Arduino → ESP32S3 Dev Module**.  
   - Select your **Port** under **Tools → Port**.

2. **Project Files**  
   - Copy all project files (including `Type_D_EXP.ino` and related `.cpp`/`.h` files) into your Arduino sketch folder.

3. **Install Libraries**  
   - Use Library Manager or "Add .ZIP Library..." to install:  
     - AsyncTCP  
     - ESPAsyncWebServer  


5. **Build and Upload**  
   - Open `Type_D_EXP.ino` in Arduino IDE.  
   - Click **Upload**.

## Basic API: `udp_stat`

Send statistics over UDP.

```cpp
#include "udp_stat.h"

// Initialize UDP statistics module
UDPStat::begin(uint16_t port);

// Send a statistic
UDPStat::sendStat(const char* name, int value);

// Example:
void setup() {
    UDPStat::begin(8087); // Start UDP on port 8087
}

void loop() {
    UDPStat::sendStat("temperature", 25);
    delay(1000);
}
