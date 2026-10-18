#pragma once

#include <Arduino.h>

#include "Camera.h"

namespace Somnium {

class Host {
 public:
  using PumpFn = void (*)();

  static void poll();
  static void setPump(PumpFn fn);

  // Unknown operator lines (portal, link, wificlear, wificook). USB cook
  // commands stay in Host. Return true when the line was consumed.
  using LineFn = bool (*)(const char *line);
  static void setLineHook(LineFn fn);

  // Cook via USB serial bridge (Python on ThinkPad → local /v1/process).
  // On success, outDream owns a heap/PSRAM buffer (caller frees).
  static bool cook(const uint8_t *jpeg, size_t jpegLen, const char *styleId,
                   const char *takenAt, float lat, float lon,
                   JpegBuffer &outDream, String *err = nullptr);
};

}  // namespace Somnium
