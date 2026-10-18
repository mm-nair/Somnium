#pragma once

#include <Arduino.h>

namespace Somnium {
namespace App {

struct Chrono {
  // Uses wall clock if Host received TIME <unix> from the USB bridge.
  static bool synced();
  static void formatDateTime(char *buf, size_t len);
  static void formatDate(char *buf, size_t len);
  static void formatTime(char *buf, size_t len);
};

}  // namespace App
}  // namespace Somnium
