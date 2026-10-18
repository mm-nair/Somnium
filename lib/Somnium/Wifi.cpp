#include "Wifi.h"

#include <WiFi.h>

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
bool gDidScan = false;
unsigned long gLastStatusLogMs = 0;
int gLastStatus = -1;
unsigned long gLastReconnectMs = 0;

const char *statusName(wl_status_t st) {
  switch (st) {
    case WL_IDLE_STATUS:
      return "idle";
    case WL_NO_SSID_AVAIL:
      return "no_ssid";
    case WL_SCAN_COMPLETED:
      return "scan_done";
    case WL_CONNECTED:
      return "connected";
    case WL_CONNECT_FAILED:
      return "fail";
    case WL_CONNECTION_LOST:
      return "lost";
    case WL_DISCONNECTED:
      return "disconnected";
    default:
      return "?";
  }
}

void logStatus(bool force) {
  const int st = static_cast<int>(WiFi.status());
  const unsigned long now = millis();
  if (!force && st == gLastStatus && now - gLastStatusLogMs < 4000) {
    return;
  }
  gLastStatus = st;
  gLastStatusLogMs = now;
  Console::printf("WIFI status %s (%d)\n", statusName(static_cast<wl_status_t>(st)),
                  st);
}

void scanNearby() {
  Console::println("WIFI scan…");
  const int n = WiFi.scanNetworks(/*async=*/false, /*hidden=*/true);
  if (n <= 0) {
    Console::printf("WIFI scan empty (%d)\n", n);
    return;
  }
  Console::printf("WIFI saw %d AP(s):\n", n);
  for (int i = 0; i < n && i < 12; ++i) {
    Console::printf("  [%d] '%s' ch=%d rssi=%d\n", i, WiFi.SSID(i).c_str(),
                    WiFi.channel(i), WiFi.RSSI(i));
  }
  WiFi.scanDelete();
}

}  // namespace

bool Wifi::begin() {
  if (strlen(wifiNetworkName) == 0) {
    Console::println("WIFI no credentials");
    return false;
  }
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);
  WiFi.disconnect(false /*wifioff*/, false /*eraseap*/);
  delay(50);
  Console::printf("WIFI begin ssid='%s' (len=%u)\n", wifiNetworkName,
                  static_cast<unsigned>(strlen(wifiNetworkName)));
  WiFi.begin(wifiNetworkName, wifiNetworkPassword);
  gStarted = true;
  gLoggedConnected = false;
  gDidScan = false;
  gLastStatus = -1;
  gLastReconnectMs = millis();
  logStatus(true);
  return true;
}

bool Wifi::waitConnected(unsigned long timeoutMs) {
  if (!gStarted) {
    return false;
  }
  const unsigned long startedAt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < timeoutMs) {
    logStatus(false);
    if (!gDidScan && millis() - startedAt > 6000) {
      gDidScan = true;
      scanNearby();
      // Re-issue join after scan (scan can abort an in-flight connect).
      WiFi.begin(wifiNetworkName, wifiNetworkPassword);
    }
    delay(250);
    yield();
  }
  if (WiFi.status() != WL_CONNECTED) {
    if (!gDidScan) {
      scanNearby();
    }
    logStatus(true);
    Console::println("WIFI give up");
    return false;
  }
  gLoggedConnected = true;
  Console::printf("WIFI %s %d dBm\n", WiFi.localIP().toString().c_str(),
                  WiFi.RSSI());
  return true;
}

bool Wifi::isConnected() { return WiFi.status() == WL_CONNECTED; }

void Wifi::poll() {
  if (!gStarted) {
    return;
  }
  if (isConnected()) {
    if (!gLoggedConnected) {
      gLoggedConnected = true;
      Console::printf("WIFI %s %d dBm\n", WiFi.localIP().toString().c_str(),
                      WiFi.RSSI());
    }
    return;
  }
  gLoggedConnected = false;
  logStatus(false);
  // Periodic reconnect + one diagnostic scan.
  const unsigned long now = millis();
  if (now - gLastReconnectMs > 12000) {
    gLastReconnectMs = now;
    if (!gDidScan) {
      gDidScan = true;
      scanNearby();
    }
    Console::println("WIFI re-begin");
    WiFi.begin(wifiNetworkName, wifiNetworkPassword);
  }
}

String Wifi::ipAddress() {
  return isConnected() ? WiFi.localIP().toString() : String();
}

int Wifi::rssi() { return isConnected() ? WiFi.RSSI() : 0; }

}  // namespace Somnium
