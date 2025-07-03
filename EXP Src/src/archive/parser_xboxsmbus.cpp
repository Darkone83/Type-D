#include "parser_xboxsmbus.h"
#include "cache_manager.h"
#include <Arduino.h>

// The polling code now passes a status struct with all fields already filled in
namespace Parser_XboxSMBus {

void parse(const XboxSMBusStatus &status) {
    // You can apply any sanity/range checks here, or just set directly
    Cache_Manager::setFanSpeed(status.fanSpeed);      // already 0–100%
    Cache_Manager::setCpuTemp(status.cpuTemp);
    Cache_Manager::setAmbientTemp(status.boardTemp);  // or status.ambientTemp

    // TODO: If you ever get app name from polling (not typical), add here:
    // Cache_Manager::setCurrentApp(status.app);
}

// Optional: Print the current cache status for debug
void printStatus() {
    const XboxStatus &st = Cache_Manager::getStatus();
    Serial.printf("[XboxSMBus] Fan: %d%% | CPU: %dC | Ambient: %dC",
                  st.fanSpeed, st.cpuTemp, st.ambientTemp);
}

} // namespace Parser_XboxSMBus
