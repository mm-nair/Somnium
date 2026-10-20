#pragma once

#include <Arduino.h>

#include "Camera.h"

namespace Somnium {

class Host {
 public:
  using PumpFn = void (*)();

  static void poll();
  static void setPump(PumpFn fn);

  // UI stages for an in-flight cook. Tokens are "send", "style", and
  // "receive". The callback must not read or write Serial. "receive" fires
  // after the DREAM header and before READY DREAM, so the panel can show
  // that stage before the JPEG bytes (the wait loop is the only place that
  // pumps UI during the transfer).
  using ProgressFn = void (*)(const char *stage);
  static void setProgress(ProgressFn fn);

  // Cook via USB serial bridge (Python on ThinkPad → local /v1/process).
  // On success, outDream owns a heap/PSRAM buffer (caller frees).
  static bool cook(const uint8_t *jpeg, size_t jpegLen, const char *styleId,
                   const char *takenAt, float lat, float lon,
                   JpegBuffer &outDream, String *err = nullptr);
};

}  // namespace Somnium
