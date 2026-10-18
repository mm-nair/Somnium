#pragma once

#include <Arduino.h>

#include "Camera.h"

namespace Somnium {

class HttpCook {
 public:
  using PumpFn = void (*)();
  using NoteFn = void (*)(const char *);

  static void setPump(PumpFn fn);
  static void setNote(NoteFn fn);

  // POST /v1/process. On success, outDream owns a PSRAM/heap buffer.
  // A configured gallery upload runs after the dream is in hand and cannot
  // fail the cook. Retries are in-request only; nothing is queued on the SD card.
  static bool process(const uint8_t *jpeg, size_t jpegLen, const char *styleId,
                      const char *takenAt, float lat, float lon, JpegBuffer &outDream,
                      String *err = nullptr);
};

}  // namespace Somnium
