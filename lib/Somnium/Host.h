#pragma once

#include <Arduino.h>

#include "Camera.h"

namespace Somnium {

class Host {
 public:
  using PumpFn = void (*)();

  static void poll();
  static void setPump(PumpFn fn);

  // Cook via USB serial bridge (Python on ThinkPad → local /v1/process).
  // On success, outDream owns a heap/PSRAM buffer (caller frees).
  // deviceId and boardId are optional COOK tokens (device=, board=).
  // Null or empty leaves the header unchanged for older bridges.
  static bool cook(const uint8_t *jpeg, size_t jpegLen, const char *styleId,
                   const char *takenAt, float lat, float lon,
                   JpegBuffer &outDream, String *err = nullptr,
                   const char *deviceId = nullptr,
                   const char *boardId = nullptr);
};

}  // namespace Somnium
