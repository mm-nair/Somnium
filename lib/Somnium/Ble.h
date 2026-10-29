#pragma once

#include <Arduino.h>

namespace Somnium {

// Phone companion link. Control, pairing, and a small preview ride BLE.
// Full JPEGs use Transfer (a short-lived SoftAP). This board does not
// join a home access point.
class Ble {
 public:
  enum class Command : uint8_t { None, Shutter, Select, Mode };

  struct Pending {
    Command command = Command::None;
    char arg[16] = {};
  };

  static void begin();
  static void poll();
  static void suspend();
  static void resume();

  static bool connected();
  static bool paired();
  static bool pairPending();
  static void acceptPair();
  static bool previewBusy();

  static void setStatusContext(const char *modeId, int queueDepth);
  static void startPreview(uint32_t photoId, const uint8_t *jpeg, size_t length,
                           uint32_t crc);
  static void notifySession(const char *ssid, const char *password, uint32_t photoId);

  static bool takeCommand(Pending &out);
  static bool takePull(uint32_t &photoId);
};

}  // namespace Somnium
