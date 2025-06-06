#include "i2c_sniffer.h"
#include "parser_xboxsmbus.h"
#include "cache_manager.h"
#include "espnow_transmitter.h"

#define I2C_SDA_PIN 21   // Adjust as needed
#define I2C_SCL_PIN 22

void setup() {
  Serial.begin(115200);
  delay(500);

  Cache_Manager::begin();
  I2C_Sniffer::begin(I2C_SDA_PIN, I2C_SCL_PIN);
  ESPNow_Transmitter::begin();

  Serial.println("Xbox SMBus ESP32 sniffer started.");
}

void loop() {
  I2C_Event evt;
  if (I2C_Sniffer::getNextEvent(evt)) {
    Parser_XboxSMBus::parse(evt);
  }

  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 1000) {
    lastPrint = millis();
    Parser_XboxSMBus::printStatus();
  }

  ESPNow_Transmitter::loop();
}
