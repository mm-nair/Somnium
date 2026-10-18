#pragma once

#include <Arduino.h>

#include "Camera.h"

namespace Somnium {

class Host {
 public:
  static void poll();

  // Cook via USB serial bridge (Python on ThinkPad → local /v1/process).
  // On success, outDream owns a heap/PSRAM buffer (caller frees).
  static bool cook(const uint8_t *jpeg, size_t jpegLen, const char *styleId,
                   const char *takenAt, float lat, float lon,
                   JpegBuffer &outDream, String *err = nullptr);
};

}  // namespace Somnium
