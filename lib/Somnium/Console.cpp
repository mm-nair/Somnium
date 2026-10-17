#include "Console.h"

#include <stdarg.h>

namespace Somnium {

void Console::begin(uint32_t baud) {
  Serial.begin(baud);
  delay(500);
}

void Console::println(const char *message) { Serial.println(message); }

void Console::printf(const char *format, ...) {
  char buffer[256];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  Serial.print(buffer);
}

}  // namespace Somnium
