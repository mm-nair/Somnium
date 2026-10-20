#pragma once

#include <Arduino.h>

#include "Camera.h"

namespace Somnium {

// HttpCook.cpp installs these when include/server_config.h names a host.
// Until then cooks stay on the Wi-Fi TCP client, or USB serial.
struct HttpOps {
  bool (*configured)();
  bool (*cook)(const uint8_t *jpeg, size_t jpegLen, const char *styleId,
               const char *takenAt, float lat, float lon, JpegBuffer &outDream,
               String *err, void (*pump)());
};

void httpBind(const HttpOps &ops);
bool httpCookConfigured();

// Referenced from startup so HttpCook.cpp is linked. Its constructor
// installs the direct HTTPS cook.
void httpAnchor();

// App.cpp shows "wifi > server..." while a direct cook is configured.
void appDirectCookRegister(bool (*configured)());

class Host {
 public:
  using PumpFn = void (*)();

  static void poll();
  static void setPump(PumpFn fn);

  // Cook via direct HTTP(S), the Wi-Fi TCP bridge, or USB serial.
  // On success, outDream owns a heap/PSRAM buffer (caller frees).
  static bool cook(const uint8_t *jpeg, size_t jpegLen, const char *styleId,
                   const char *takenAt, float lat, float lon,
                   JpegBuffer &outDream, String *err = nullptr);
};

}  // namespace Somnium
