#pragma once

#include <Arduino.h>

namespace Somnium {

// USB-UART serial helpers (Freenove UART / CH343 connector).
class Console {
 public:
  static void begin(uint32_t baud);
  static void println(const char *message);
  static void printf(const char *format, ...);
  static void printBoardInfo();
};

}  // namespace Somnium
