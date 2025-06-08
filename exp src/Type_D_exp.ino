#include "i2c_sniffer.h"
#include "parser_xboxsmbus.h"
#include "cache_manager.h"
#include "espnow_transmitter.h"
#include "debug_logger.h"

#define I2C_SDA_PIN 7   // Set to your working SMBus SDA pin
#define I2C_SCL_PIN 6   // Set to your working SMBus SCL pin

void setup() {
  Serial.begin(115200);
  delay(200);  // Give time for Serial to initialize

  Cache_Manager::begin();
  I2C_Sniffer::begin(I2C_SDA_PIN, I2C_SCL_PIN);
  ESPNow_Transmitter::begin();

  Serial.println("Xbox SMBus ESP32 sniffer started.");
}

void loop() {
  // Run the sniffer and parser as often as possible for best accuracy
  I2C_Event evt;
  while (I2C_Sniffer::getNextEvent(evt)) {   // <-- changed to while for fast dequeuing
    Debug_Logger::logI2CEvent(evt);
    Parser_XboxSMBus::parse(evt);
  }

  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 1000) {
    lastPrint = millis();
    Parser_XboxSMBus::printStatus();
  }

  ESPNow_Transmitter::loop(); // Still called every loop for timely transmission
}
