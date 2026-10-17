#pragma once

#include <Arduino.h>

namespace Somnium {

class TimeSync {
 public:
  static void begin();
  static bool ready();
  // ISO-8601 UTC into buf (needs >= 25 bytes). Returns false if clock unset.
  static bool formatUtc(char *buf, size_t buflen);
};

}  // namespace Somnium
