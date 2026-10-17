#include "Console.h"

#include <stdarg.h>

#include "Config.h"

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

void Console::printBoardInfo() {
  Serial.printf("Somnium boot — flash %u B, PSRAM %u B, free heap %u B\n",
                ESP.getFlashChipSize(), ESP.getPsramSize(), ESP.getFreeHeap());
  Serial.printf(
      "Pins: LCD SCK=%d MOSI=%d DC=%d CS=%d RST=%d | ENC CLK=%d DT=%d SW=%d | "
      "SHUTTER=%d\n",
      Pins::kDisplaySck, Pins::kDisplayMosi, Pins::kDisplayDc, Pins::kDisplayCs,
      Pins::kDisplayRst, Pins::kEncoderClk, Pins::kEncoderDt, Pins::kEncoderSw,
      Pins::kShutterButton);
}

}  // namespace Somnium
