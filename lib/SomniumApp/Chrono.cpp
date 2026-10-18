#include "Chrono.h"

#include <stdio.h>
#include <time.h>

namespace Somnium {
namespace App {

void Chrono::formatDateTime(char *buf, size_t len) {
  if (buf == nullptr || len < 12) {
    return;
  }
  char date[16];
  char tod[16];
  formatDate(date, sizeof(date));
  formatTime(tod, sizeof(tod));
  snprintf(buf, len, "%s  %s", date, tod);
}

void Chrono::formatDate(char *buf, size_t len) {
  // No NTP without Wi‑Fi — event label is fine on the panel.
  snprintf(buf, len, "DubHacks");
}

void Chrono::formatTime(char *buf, size_t len) {
  const unsigned long sec = millis() / 1000;
  snprintf(buf, len, "%02lu:%02lu", (sec / 60) % 100, sec % 60);
}

}  // namespace App
}  // namespace Somnium
