// detect.h

#pragma once
#include <Arduino.h>

namespace Detect {

  // Call in setup()
  void begin();

  // Call periodically in loop()
  void loop();

  // Returns currently assigned device ID (1–4)
  uint8_t getId();

  // Optionally triggers immediate ID re-assignment (e.g., external event)
  void assignId();

  // (Advanced) Manually force status broadcast now
  void broadcastId();

  // (Advanced) Manually check for ID conflict now
  void checkIdConflict();

} // namespace Detect
