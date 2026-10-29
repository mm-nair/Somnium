#pragma once

#include <Arduino.h>

#include "Camera.h"

namespace Somnium {

struct RelayOutcome {
  char message[48] = {};
  JpegBuffer dream;
};

// Short-lived SoftAP plus a tiny HTTP server. The phone joins, pulls the
// original with Range, and PUTs the edited JPEG back. Photos stay queued
// on the SD card when no paired phone is connected.
class Transfer {
 public:
  static void begin();
  static void poll();
  static void shutdown();
  static bool radioUp();
  static int queueDepth();

  // Saves queue metadata, pushes a BLE preview, and if a phone is paired
  // brings up the AP and waits for the dream JPEG. `jpeg` stays owned by
  // the caller. On success, `out.dream` is owned by the caller.
  static bool relay(const uint8_t *jpeg, size_t length, uint32_t crc,
                    const char *style, const char *origPath, const char *dreamPath,
                    RelayOutcome &out, void (*pump)());
};

}  // namespace Somnium
