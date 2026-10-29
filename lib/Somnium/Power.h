#pragma once

#include <stdint.h>

namespace Somnium {

// Inactivity deep sleep. Defaults::kInactivitySleepMs is the only timeout.
// A shutter press wakes the chip; setup() runs again and the mode screen
// is the ready state. See Power.cpp for which pin can actually wake.
class Power {
 public:
  // RAII: capture, save, and transfer hold this so poll() will not sleep.
  class Busy {
   public:
    Busy();
    ~Busy();
    Busy(const Busy &) = delete;
    Busy &operator=(const Busy &) = delete;
  };

  static void begin();
  static void noteActivity();
  static void poll();
  static bool wokeFromSleep();

 private:
  static void enterDeepSleep();
};

}  // namespace Somnium
