#pragma once

#include <stdint.h>

namespace Somnium {

// Wi-Fi and server settings. NVS (Preferences namespace "somnium") wins after
// the setup portal saves. Until then, compile-time defaults from
// include/wifi_credentials.h and include/server_config.h are used.
struct NetSettings {
  char ssid[33];
  char password[65];
  char host[80];
  uint16_t port;
  bool tls;
  char path[64];
  char galleryUrl[160];
  char galleryToken[128];
  bool provisioned;
};

class NetConfig {
 public:
  static void load();
  static bool save(const NetSettings &settings);
  static void clear();
  static const NetSettings &get();
  static bool hasWifi();
};

}  // namespace Somnium
