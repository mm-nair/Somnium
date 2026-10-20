#pragma once

// Copy to include/wifi_secrets.h and fill in the home network.
// include/wifi_secrets.h is gitignored. An empty SSID leaves Wi-Fi off
// and the camera keeps cooking over USB.
//
// These are C string literals. Escape \ and " if they appear in the password.
// SSID is at most 32 characters. Password is at most 64.

#define SOMNIUM_WIFI_SSID ""
#define SOMNIUM_WIFI_PASSWORD ""
