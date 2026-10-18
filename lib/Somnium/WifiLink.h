#pragma once

#include <Arduino.h>
#include <WiFi.h>

#include "LinkPlan.h"

namespace Somnium {

class WifiLink {
 public:
  static void begin();
  static void poll();
  static void reload();
  static void requestPortal();
  static void setBusy(bool busy);
  static bool isOnline();
  static Link::Phase phase();
  static const char *headerLabel();
  static void printStatus();
  // Resolves somnium.local (and any other .local host in the saved config).
  static bool resolveServer(IPAddress *ip, bool *haveIp);
};

}  // namespace Somnium
