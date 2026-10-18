#include "Console.h"

#include <stdarg.h>

namespace Somnium {

void Console::begin(uint32_t baud) {
  // DREAM JPEGs are ~10–40 KB; default 256 B RX overflows while the Process
  // page SPI-blits, so Host::cook hangs in readExact forever.
  Serial.setRxBufferSize(64 * 1024);
  Serial.setTxBufferSize(4 * 1024);
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
