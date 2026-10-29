#pragma once

#include <Arduino.h>
#include <Adafruit_SPITFT.h>

namespace Somnium {

class Display {
 public:
  static bool begin();
  static void clear(uint16_t color = 0x0000);
  static int width();
  static int height();
  static bool ready();
  static Adafruit_SPITFT *tft();
  // Panel sleep plus backlight. Light sleep calls this around the CPU halt.
  static void setActive(bool on);
  static bool active();
};

}  // namespace Somnium
