#pragma once

#include <Arduino.h>

namespace Somnium {

class Wifi {
 public:
  // Starts STA join. Returns true if credentials exist and begin() was called.
  static bool begin();

  // Blocking wait up to timeoutMs. Returns true when connected.
  static bool waitConnected(unsigned long timeoutMs);

  // Non-blocking: true while associated.
  static bool isConnected();

  // Call from loop to log once when association succeeds in the background.
  static void poll();

  static String ipAddress();
  static int rssi();
};

}  // namespace Somnium
