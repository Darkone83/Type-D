// File: parser_xboxsmbus.h

#ifndef PARSER_XBOXSAMBUS_H
#define PARSER_XBOXSAMBUS_H

#include "i2c_sniffer.h"

namespace Parser_XboxSMBus {
  void parse(const I2C_Event &evt);
  void printStatus();
}

#endif
