#pragma once
#include <TFT_eSPI.h>
#include "espnow_receiver.h"

namespace xbox_status {
    void show(TFT_eSPI* tft, const XboxPacket& packet);
}
