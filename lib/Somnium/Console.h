#pragma once

#include <Arduino.h>

namespace Somnium {

class Console {
 public:
  static void begin(uint32_t baud);
  static void println(const char *message);
  static void printf(const char *format, ...);
};

}  // namespace Somnium
