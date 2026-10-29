#include "Link.h"

#include <string.h>
#include <time.h>

#include <ESPmDNS.h>
#include <WiFi.h>
#include <lwip/sockets.h>

#include "Config.h"
#include "Console.h"

#if __has_include("wifi_secrets.h")
#include "wifi_secrets.h"
#endif
#ifndef SOMNIUM_WIFI_SSID
#define SOMNIUM_WIFI_SSID ""
#endif
#ifndef SOMNIUM_WIFI_PASSWORD
#define SOMNIUM_WIFI_PASSWORD ""
#endif

namespace Somnium {
namespace {

bool gEnabled = false;
bool gBusy = false;
bool gServices = false;
bool gMdns = false;
bool gWasConnected = false;
unsigned long gLastBegin = 0;
WiFiServer gServer(Defaults::kLinkPort);
WiFiClient gClient;

bool secretsOk() {
  const size_t ssidLen = strlen(SOMNIUM_WIFI_SSID);
  const size_t passLen = strlen(SOMNIUM_WIFI_PASSWORD);
  if (ssidLen == 0) {
    return false;
  }
  if (ssidLen > 32 || passLen > 64) {
    Console::println("WIFI secrets rejected");
    return false;
  }
  return true;
}

void tune(WiFiClient &client) {
  client.setNoDelay(true);
  client.setTimeout(50);
  const int fd = client.fd();
  if (fd < 0) {
    return;
  }
  const int yes = 1;
  const int idle = 5;
  const int interval = 2;
  const int probes = 3;
  setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof(yes));
  setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
  setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &interval, sizeof(interval));
  setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &probes, sizeof(probes));
}

void startServices() {
  if (!gServices) {
    gServer.begin(Defaults::kLinkPort);
    gServer.setNoDelay(true);
    gServices = true;
    Console::printf("WIFI %s ip=%s port=%u\n", Defaults::kLinkHostname,
                    WiFi.localIP().toString().c_str(),
                    static_cast<unsigned>(Defaults::kLinkPort));
  }
  if (!gMdns && MDNS.begin(Defaults::kLinkHostname)) {
    MDNS.addService("somnium", "tcp", Defaults::kLinkPort);
    gMdns = true;
    Console::printf("WIFI mdns %s.local\n", Defaults::kLinkHostname);
  }
}

void stopServices() {
  if (gClient) {
    gClient.stop();
  }
  if (gMdns) {
    MDNS.end();
    gMdns = false;
  }
  if (gServices) {
    gServer.end();
    gServices = false;
  }
}

void acceptClients() {
  if (gBusy || !gServices) {
    return;
  }
  while (gServer.hasClient()) {
    WiFiClient incoming = gServer.accept();
    if (!incoming) {
      break;
    }
    tune(incoming);
    if (gClient) {
      gClient.stop();
    }
    gClient = incoming;
    Console::println("WIFI client");
  }
  if (gClient && !gClient.connected()) {
    gClient.stop();
  }
}

int portAvailable(void *ctx) {
  auto *client = static_cast<WiFiClient *>(ctx);
  const int ready = client->available();
  if (ready > 0) {
    return ready;
  }
  // Buffered bytes still belong to the cook. A dead socket with nothing
  // left should fail the wait instead of sitting until kCookTimeoutMs.
  if (!client->connected()) {
    return -1;
  }
  return 0;
}

int portRead(void *ctx) { return static_cast<WiFiClient *>(ctx)->read(); }

size_t portReadBytes(void *ctx, uint8_t *dst, size_t len) {
  return static_cast<WiFiClient *>(ctx)->readBytes(dst, len);
}

size_t portWrite(void *ctx, const uint8_t *data, size_t len) {
  auto *client = static_cast<WiFiClient *>(ctx);
  size_t sent = 0;
  const unsigned long started = millis();
  while (sent < len) {
    if (!client->connected()) {
      break;
    }
    const size_t n = client->write(data + sent, len - sent);
    if (n > 0) {
      sent += n;
      continue;
    }
    if (millis() - started > 20000) {
      break;
    }
    yield();
    delay(1);
  }
  return sent;
}

void portFlush(void *ctx) { static_cast<WiFiClient *>(ctx)->flush(); }

bool clientConnected() { return static_cast<bool>(gClient) && gClient.connected(); }

void beginWifi() {
  if (!secretsOk()) {
    Console::println("WIFI off");
    return;
  }
  gEnabled = true;
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.setHostname(Defaults::kLinkHostname);
  WiFi.begin(SOMNIUM_WIFI_SSID, SOMNIUM_WIFI_PASSWORD);
  gLastBegin = millis();
  linkRegister(clientConnected);
  Console::printf("WIFI join %s\n", SOMNIUM_WIFI_SSID);
}

void pollWifi() {
  if (!gEnabled) {
    return;
  }
  const bool up = WiFi.status() == WL_CONNECTED;
  if (!up) {
    if (gWasConnected) {
      stopServices();
      gWasConnected = false;
      Console::println("WIFI down");
    }
    if (!gBusy && millis() - gLastBegin > 15000) {
      gLastBegin = millis();
      WiFi.disconnect(false, false);
      delay(100);
      WiFi.setHostname(Defaults::kLinkHostname);
      WiFi.begin(SOMNIUM_WIFI_SSID, SOMNIUM_WIFI_PASSWORD);
      Console::println("WIFI retry");
    }
    return;
  }
  if (!gWasConnected) {
    // Certificate checks need a real clock. Direct HTTPS also waits, but
    // starting SNTP as soon as the radio associates overlaps the wait.
    configTime(0, 0, "pool.ntp.org", "time.nist.gov", "time.google.com");
  }
  gWasConnected = true;
  startServices();
  acceptClients();
}

void setBusy(bool busy) { gBusy = busy; }

bool claim(CookPort *port) {
  if (port == nullptr || !clientConnected()) {
    return false;
  }
  port->ctx = &gClient;
  port->available = portAvailable;
  port->read = portRead;
  port->readBytes = portReadBytes;
  port->write = portWrite;
  port->flush = portFlush;
  return true;
}

int idleRead() {
  if (gBusy || !clientConnected() || gClient.available() <= 0) {
    return -1;
  }
  return gClient.read();
}

void dropClient() {
  if (gClient) {
    gClient.stop();
  }
}

void shutdownRadio() {
  const bool started = gEnabled || gWasConnected || gServices;
  gEnabled = false;
  stopServices();
  if (!started) {
    Console::println("WIFI idle");
    return;
  }
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);
  Console::println("WIFI off");
}

struct BindLink {
  BindLink() {
    // Function pointers only. Wi-Fi itself starts from setup(), not here.
    LinkOps ops{};
    ops.begin = beginWifi;
    ops.poll = pollWifi;
    ops.setBusy = setBusy;
    ops.claim = claim;
    ops.idleRead = idleRead;
    ops.drop = dropClient;
    linkBind(ops);
  }
};

BindLink gBindLink __attribute__((used));

}  // namespace

void linkAnchor() {}

void linkShutdown() { shutdownRadio(); }

}  // namespace Somnium
