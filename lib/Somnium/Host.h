#pragma once

#include <Arduino.h>

#include "Camera.h"

namespace Somnium {

// Optional COOK fields. Negative sentinels are omitted from the header.
// sequence 0 is omitted. deviceId may be null. Pointers must outlive cook().
struct CookExtras {
  uint32_t sequence = 0;
  int queueDepth = -1;
  int batteryPctTimes10 = -1;
  int batteryMv = -1;
  int charging = -1;
  int lowBattery = -1;
  const char *deviceId = nullptr;
};

class Host {
 public:
  using PumpFn = void (*)();
  using LinkFn = void (*)();
  using LineFn = void (*)(const char *line);

  static void poll();
  static void setPump(PumpFn fn);
  // Fired on TIME and hello. Must not start a cook; the serial parser is active.
  static void setOnLink(LinkFn fn);
  // Operator lines that are not part of the cook protocol (bat, queue, sleep).
  static void setLineHook(LineFn fn);

  // Cook via USB serial bridge (Python on ThinkPad → local /v1/process).
  // On success, outDream owns a heap/PSRAM buffer (caller frees).
  static bool cook(const uint8_t *jpeg, size_t jpegLen, const char *styleId,
                   const char *takenAt, float lat, float lon,
                   JpegBuffer &outDream, String *err = nullptr,
                   const CookExtras *extras = nullptr);
};

}  // namespace Somnium
