#include "TimeSync.h"

#include <Arduino.h>
#include <time.h>

namespace Somnium {
namespace {

bool gStarted = false;

}  // namespace

void TimeSync::begin() {
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  gStarted = true;
}

bool TimeSync::ready() {
  const time_t now = time(nullptr);
  return gStarted && now > 1700000000;
}

bool TimeSync::formatUtc(char *buf, size_t buflen) {
  if (buf == nullptr || buflen < 21) {
    return false;
  }
  const time_t now = time(nullptr);
  if (now < 1700000000) {
    return false;
  }
  struct tm parts {};
  gmtime_r(&now, &parts);
  return strftime(buf, buflen, "%Y-%m-%dT%H:%M:%SZ", &parts) > 0;
}

}  // namespace Somnium
