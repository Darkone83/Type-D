#ifndef PARSER_XBOXSAMBUS_H
#define PARSER_XBOXSAMBUS_H

#include "xbox_smbus_poll.h"

namespace Parser_XboxSMBus {
  void parse(const XboxSMBusStatus &status);
  void printStatus();
}

#endif
