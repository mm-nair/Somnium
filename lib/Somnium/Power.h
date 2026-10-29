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
  // Skip the ADC while a capture, SD write, or SoftAP transfer is in progress.
  static void sampleBattery();
  // Smoothed pack voltage at the BAT+ pad, after the divider scale. False
  // until the first quiet sample.
  static bool batteryMillivolts(int &millivolts);
  // 0..100 from 3.3 V..4.2 V, or -1 before the first sample.
  static int batteryPercent();
  static bool batteryLow();
};

class PowerBusy {
 public:
  PowerBusy() { Power::setBusy(true); }
  ~PowerBusy() { Power::setBusy(false); }
  PowerBusy(const PowerBusy &) = delete;
  PowerBusy &operator=(const PowerBusy &) = delete;
};

}  // namespace Somnium
