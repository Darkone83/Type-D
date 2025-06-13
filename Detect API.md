# Type D Device Detection Module

This module enables each Type D device to automatically assign itself a unique device ID (1–4) on a local network. It uses UDP broadcast for zero-configuration discovery, dynamic ID assignment, and ongoing conflict detection—no manual setup required.

---

## How Detection Works

- **On startup**, the device broadcasts a discovery message over UDP to all peers.
- Each active device responds with its current ID.
- The new device collects all responses, notes which IDs are in use, and picks the **lowest unused ID** in the range 1–4.
- Once assigned, the device broadcasts its ID to peers at regular intervals for ongoing presence and collision detection.
- If a conflict is detected (i.e., another device responds with the same ID from a different IP), the device automatically attempts to reassign a unique ID.

---

## API (C++)

All functions are in the `Detect` namespace:

```cpp
namespace Detect {
    void begin();              // Call once in setup() after WiFi is connected
    void loop();               // Call regularly in your main loop()
    uint8_t getId();           // Returns the current assigned device ID (1–4)
    void assignId();           // Manually trigger a new ID scan/assignment
    void broadcastId();        // Immediately announce your current ID
    void checkIdConflict();    // Scan immediately for ID conflicts
}
```

---

## Usage Example

```cpp
#include "detect.h"

void setup() {
    // ... (WiFi/network setup code) ...
    Detect::begin(); // Start device detection
}

void loop() {
    Detect::loop();  // Regular detection processing
    // ... (other application code) ...
}

// To get your device's current unique ID at any time:
uint8_t myId = Detect::getId();
```

---

## Main Functions

| Function                    | Description                                            |
|-----------------------------|-------------------------------------------------------|
| `Detect::begin()`           | Initialize detection system; call after WiFi setup    |
| `Detect::loop()`            | Periodically handles broadcast, assignment, conflicts |
| `Detect::getId()`           | Returns current device ID (1–4)                       |
| `Detect::assignId()`        | Triggers immediate ID re-assignment                   |
| `Detect::broadcastId()`     | Broadcasts current ID now (advanced use)              |
| `Detect::checkIdConflict()` | Checks for conflicting IDs immediately                |

---

## Detection Protocol

- **Discovery:**  
  Device sends a UDP broadcast to all peers at startup.
- **Response:**  
  Devices reply with their active ID (`TYPE_D_ID:<id>`).
- **Assignment:**  
  Each device picks the lowest available ID (1–4) not in use.
- **Broadcast:**  
  Devices send their ID at regular intervals for monitoring and quick detection of new units.
- **Conflict Resolution:**  
  If a device hears its own ID used by another IP, it waits a random short time and re-triggers assignment to avoid persistent conflicts.

---

## Integration Tips

- **Always** call `Detect::begin()` in your `setup()` after WiFi is established.
- **Call `Detect::loop()`** as part of your main Arduino `loop()` to maintain detection and status.
- Use `Detect::getId()` whenever you need your assigned unique device ID in your application logic.

---

*This documentation matches your actual `detect.h` and `detect.cpp` source. For any custom logic, timing, or protocol changes, refer to the source code or request more detail here.*
