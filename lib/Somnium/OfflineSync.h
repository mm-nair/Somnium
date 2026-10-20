#pragma once

#include <Arduino.h>

namespace Somnium {

class OfflineSync {
 public:
  using PumpFn = void (*)();

  static void begin();
  static void setPump(PumpFn fn);
  // Serial link is up (TIME / hello). Does not sync inside the callback.
  static void noteLink();
  static void tick();
  static bool linkUp();
};

}  // namespace Somnium
