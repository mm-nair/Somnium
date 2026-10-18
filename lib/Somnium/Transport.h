#pragma once

#include <Arduino.h>

#include "Camera.h"

namespace Somnium {

enum class TransportKind : uint8_t { None, Wifi, Usb };

// Wi-Fi is the primary cook path. USB serial stays the fallback, so a shot
// still completes when the network is down or the HTTP cook fails.
//
// Owned here: station join, setup portal, HTTP cook, USB fallback.
// Not owned here: screen layout (call headerLabel / the cook status note),
// BLE, battery, an offline SD queue, the style engine, multi-camera identity.
class Transport {
 public:
  using PumpFn = void (*)();
  using StatusFn = void (*)(const char *status);

  static void begin();
  static void poll();
  static void setPump(PumpFn fn);
  static void setStatus(StatusFn fn);
  static void requestPortal();
  static const char *headerLabel();
  static TransportKind lastPath();

  // On success, outDream owns a heap/PSRAM buffer (caller frees).
  static bool cook(const uint8_t *jpeg, size_t jpegLen, const char *styleId,
                   const char *takenAt, float lat, float lon, JpegBuffer &outDream,
                   String *err = nullptr);
};

}  // namespace Somnium
