#pragma once

#include <Arduino.h>

namespace Somnium {

// Inactivity light sleep. The XIAO charger is a hardware path on the BAT
// pads; nothing here gates VBUS or the cell. The slide switch is also
// hardware (battery +) and has no GPIO.
class Power {
 public:
  static void begin();
  static void setBusy(bool busy);
  static bool busy();
  // True when the CPU actually halted. Caller redraws the UI afterwards.
  static bool poll();
  // No ADC pin is free on this wiring. Always unknown.
  static bool batteryMillivolts(int &millivolts);
};

class PowerBusy {
 public:
  PowerBusy() { Power::setBusy(true); }
  ~PowerBusy() { Power::setBusy(false); }
  PowerBusy(const PowerBusy &) = delete;
  PowerBusy &operator=(const PowerBusy &) = delete;
};

}  // namespace Somnium
