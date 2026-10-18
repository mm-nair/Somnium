#pragma once

#include <Arduino.h>

namespace Somnium {
namespace App {

struct Chrono {
  // Fills buf with local-ish display time. Uses UTC if NTP ready, else uptime clock.
  static void formatDateTime(char *buf, size_t len);
  static void formatDate(char *buf, size_t len);
  static void formatTime(char *buf, size_t len);
};

}  // namespace App
}  // namespace Somnium
