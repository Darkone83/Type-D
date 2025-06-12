#pragma once
#include "disp_cfg.h"
#include "espnow_receiver.h"

namespace xbox_status {
    void show(LGFX* tft, const XboxPacket& packet);
}
