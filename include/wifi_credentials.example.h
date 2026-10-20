#pragma once

// Optional compile-time Wi-Fi defaults.
// Copy to include/wifi_credentials.h (gitignored). Leave both strings empty
// to boot the Somnium-Setup portal instead. The portal writes NVS, and a
// saved portal config wins over this file until `wificlear`.

#define SOMNIUM_WIFI_SSID ""
#define SOMNIUM_WIFI_PASSWORD ""
