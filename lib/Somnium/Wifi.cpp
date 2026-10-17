#include "Wifi.h"

#include <WiFi.h>

#include "Config.h"
#include "Console.h"

#if __has_include("wifi_credentials.h")
#include "wifi_credentials.h"
#else
constexpr const char *wifiNetworkName = "";
constexpr const char *wifiNetworkPassword = "";
#endif

namespace Somnium {
namespace {

bool gStarted = false;
bool gLoggedConnected = false;

}  // namespace

bool Wifi::begin() {
  if (strlen(wifiNetworkName) == 0) {
    Console::println(
        "Wi-Fi: copy include/wifi_credentials.example.h to "
        "include/wifi_credentials.h and set 2.4 GHz credentials.");
    return false;
  }

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(wifiNetworkName, wifiNetworkPassword);
  gStarted = true;
  gLoggedConnected = false;
  Console::printf("Wi-Fi: connecting to %s ...\n", wifiNetworkName);
  return true;
}

bool Wifi::waitConnected(unsigned long timeoutMs) {
  if (!gStarted) {
    return false;
  }
  const unsigned long startedAt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < timeoutMs) {
    delay(250);
  }
  if (WiFi.status() != WL_CONNECTED) {
    Console::println("Wi-Fi: connect timed out.");
    return false;
  }
  gLoggedConnected = true;
  Console::printf("Wi-Fi: %s (%d dBm)\n", WiFi.localIP().toString().c_str(),
                  WiFi.RSSI());
  return true;
}

bool Wifi::isConnected() { return WiFi.status() == WL_CONNECTED; }

void Wifi::poll() {
  if (!gStarted || gLoggedConnected || !isConnected()) {
    return;
  }
  gLoggedConnected = true;
  Console::printf("Wi-Fi: %s (%d dBm)\n", WiFi.localIP().toString().c_str(),
                  WiFi.RSSI());
}

String Wifi::ipAddress() {
  return isConnected() ? WiFi.localIP().toString() : String();
}

int Wifi::rssi() { return isConnected() ? WiFi.RSSI() : 0; }

}  // namespace Somnium
