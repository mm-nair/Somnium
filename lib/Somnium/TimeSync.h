#pragma once

#include <stddef.h>

namespace Somnium {

class TimeSync {
 public:
  static void begin();
  static bool ready();
  // ISO-8601 UTC. buf needs at least 21 bytes.
  static bool formatUtc(char *buf, size_t buflen);
};

}  // namespace Somnium
