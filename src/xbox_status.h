#pragma once
#include <TFT_eSPI.h>
#include "espnow_receiver.h"  // For XboxStatus struct

namespace xbox_status {
    void show(TFT_eSPI* tft, const XboxStatus& status);
} // namespace xbox_status
