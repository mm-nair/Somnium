#pragma once

#include <Arduino.h>

namespace Somnium {

class Wifi {
 public:
  static bool begin();
  static bool waitConnected(unsigned long timeoutMs);
  static bool isConnected();
  static void poll();
  static String ipAddress();
  static int rssi();
};

}  // namespace Somnium
