#include "WifiLink.h"

#include <DNSServer.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "Console.h"
#include "LinkStatus.h"
#include "NetConfig.h"
#include "TimeSync.h"

namespace Somnium {
namespace {

constexpr uint32_t kJoinTimeoutMs = 12000;
constexpr uint32_t kPortalRetryMs = 3000;

WebServer gWeb(80);
DNSServer gDns;
Link::Phase gPhase = Link::Phase::Backoff;
bool gBusy = false;
bool gPortalRequested = false;
bool gRoutesReady = false;
bool gPortalUp = false;
bool gMdnsStarted = false;
bool gDidScan = false;
int gFailures = 0;
unsigned long gJoinStartedMs = 0;
unsigned long gNextJoinMs = 0;
unsigned long gPortalRetryMs = 0;

struct PageBuf {
  char *data = nullptr;
  size_t cap = 0;
  size_t used = 0;
  bool ok = true;
};

void pageFree(PageBuf *page) {
  free(page->data);
  page->data = nullptr;
}

bool pageReserve(PageBuf *page, size_t extra) {
  if (!page->ok) {
    return false;
  }
  if (page->used + extra + 1 <= page->cap) {
    return true;
  }
  size_t cap = page->cap == 0 ? 2048 : page->cap;
  while (page->used + extra + 1 > cap) {
    cap *= 2;
  }
  char *grown = static_cast<char *>(realloc(page->data, cap));
  if (grown == nullptr) {
    page->ok = false;
    return false;
  }
  page->data = grown;
  page->cap = cap;
  return true;
}

void pageAdd(PageBuf *page, const char *text) {
  if (text == nullptr) {
    return;
  }
  const size_t length = strlen(text);
  if (!pageReserve(page, length)) {
    return;
  }
  memcpy(page->data + page->used, text, length);
  page->used += length;
  page->data[page->used] = '\0';
}

void pageEsc(PageBuf *page, const char *text) {
  if (text == nullptr) {
    return;
  }
  for (const char *cursor = text; *cursor != '\0'; ++cursor) {
    const char *replacement = nullptr;
    char one[2] = {*cursor, '\0'};
    if (*cursor == '&') {
      replacement = "&amp;";
    } else if (*cursor == '<') {
      replacement = "&lt;";
    } else if (*cursor == '>') {
      replacement = "&gt;";
    } else if (*cursor == '"') {
      replacement = "&quot;";
    } else {
      replacement = one;
    }
    pageAdd(page, replacement);
  }
}

void startMdns() {
  if (gMdnsStarted) {
    return;
  }
  if (MDNS.begin("somnium-cam")) {
    gMdnsStarted = true;
    Console::println("MDNS somnium-cam.local");
  } else {
    Console::println("MDNS begin failed");
  }
}

void scanNearby() {
  Console::println("WIFI scan");
  const int count = WiFi.scanNetworks(false, true);
  if (count <= 0) {
    Console::printf("WIFI scan empty (%d)\n", count);
    return;
  }
  Console::printf("WIFI saw %d AP(s)\n", count);
  const int shown = count < 8 ? count : 8;
  for (int index = 0; index < shown; ++index) {
    Console::printf("  [%d] '%s' ch=%d rssi=%d\n", index, WiFi.SSID(index).c_str(),
                    WiFi.channel(index), WiFi.RSSI(index));
  }
  WiFi.scanDelete();
}

void startJoin() {
  const NetSettings &settings = NetConfig::get();
  if (settings.ssid[0] == '\0') {
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.setHostname("somnium-cam");
  WiFi.disconnect(false, false);
  delay(20);
  Console::printf("WIFI begin ssid='%s'\n", settings.ssid);
  if (settings.password[0] == '\0') {
    WiFi.begin(settings.ssid);
  } else {
    WiFi.begin(settings.ssid, settings.password);
  }
  gJoinStartedMs = millis();
}

void stopPortal() {
  if (!gPortalUp) {
    return;
  }
  gDns.stop();
  gWeb.stop();
  WiFi.softAPdisconnect(true);
  gPortalUp = false;
}

void redirectPortal() {
  const String target = String("http://") + WiFi.softAPIP().toString() + "/";
  gWeb.sendHeader("Location", target, true);
  gWeb.sendHeader("Cache-Control", "no-store");
  gWeb.send(302, "text/plain", "Somnium setup");
}

void sendHtml(const PageBuf &page) {
  gWeb.sendHeader("Cache-Control", "no-store");
  if (!page.ok || page.data == nullptr) {
    gWeb.send(500, "text/plain", "page");
    return;
  }
  gWeb.send(200, "text/html", page.data);
}

void renderForm(const char *error) {
  const NetSettings &settings = NetConfig::get();
  PageBuf page;
  pageAdd(&page,
          "<!doctype html><html><head><meta name=viewport "
          "content=\"width=device-width,initial-scale=1\">"
          "<title>Somnium Setup</title><style>"
          "body{font-family:sans-serif;background:#140c12;color:#ff69b4;"
          "margin:0;padding:24px;max-width:28rem}"
          "input,button{font-size:16px}input[type=text],input[type=password],"
          "input:not([type]){width:100%;padding:10px;margin:6px 0 14px;"
          "box-sizing:border-box}"
          "button{background:#ff69b4;color:#140c12;border:0;padding:12px 18px;"
          "font-weight:700}small,p.note{color:#c8a0b4}"
          "p.err{color:#fff}"
          "</style></head><body><h1>Somnium</h1>"
          "<p class=note>2.4 GHz only. This camera cannot join a 5 GHz network. "
          "Re-enter the Wi-Fi password. Leave it blank for an open network.</p>");
  if (error != nullptr && error[0] != '\0') {
    pageAdd(&page, "<p class=err>");
    pageEsc(&page, error);
    pageAdd(&page, "</p>");
  }
  pageAdd(&page, "<form method=POST action=/save>");
  pageAdd(&page, "<label>Network</label><input name=ssid maxlength=32 required value=\"");
  pageEsc(&page, settings.ssid);
  pageAdd(&page, "\"><label>Password</label><input name=password type=password maxlength=64>");
  pageAdd(&page, "<label>Server host</label><input name=host maxlength=79 value=\"");
  pageEsc(&page, settings.host[0] != '\0' ? settings.host : "somnium.local");
  pageAdd(&page, "\"><label>Port</label><input name=port maxlength=5 value=\"");
  char port[8];
  snprintf(port, sizeof(port), "%u", settings.port == 0 ? 8080 : settings.port);
  pageEsc(&page, port);
  pageAdd(&page, "\"><label>Path</label><input name=path maxlength=63 value=\"");
  pageEsc(&page, settings.path[0] != '\0' ? settings.path : Link::kDefaultPath);
  pageAdd(&page, settings.tls ? "\"><label><input type=checkbox name=tls value=1 checked> HTTPS"
                               "</label>"
                               : "\"><label><input type=checkbox name=tls value=1> HTTPS</label>");
  pageAdd(&page,
          "<label>Gallery URL (optional)</label>"
          "<input name=gallery_url maxlength=159 placeholder="
          "\"https://somnium-red.vercel.app/api/upload\" value=\"");
  pageEsc(&page, settings.galleryUrl);
  pageAdd(&page,
          "\"><label>Gallery bearer token (optional)</label>"
          "<input name=gallery_token type=password maxlength=127 autocomplete=off>");
  if (settings.galleryToken[0] != '\0') {
    pageAdd(&page,
            "<p class=note>A gallery token is already stored on this camera. "
            "Leave the field blank to keep it.</p>"
            "<label><input type=checkbox name=clear_token value=1> Clear gallery token</label>");
  }
  pageAdd(&page,
          "<p><button type=submit>Save and connect</button></p></form>"
          "<p class=note>The token stays in NVS on the camera. It is not written "
          "to the laptop repo. Hold shutter and the dial for 1.5s to open this "
          "page again.</p></body></html>");
  sendHtml(page);
  pageFree(&page);
}

void handleRoot() { renderForm(nullptr); }

bool copyArg(char *dst, size_t capacity, const String &value) {
  if (value.length() + 1 > capacity) {
    return false;
  }
  memcpy(dst, value.c_str(), value.length() + 1);
  return true;
}

void handleSave() {
  NetSettings next = NetConfig::get();
  const String ssid = gWeb.arg("ssid");
  const String password = gWeb.arg("password");
  const String host = gWeb.arg("host");
  const String port = gWeb.arg("port");
  const String path = gWeb.arg("path");
  const String gallery = gWeb.arg("gallery_url");
  const String token = gWeb.arg("gallery_token");
  const bool tls = gWeb.hasArg("tls");
  const bool clearToken = gWeb.hasArg("clear_token");

  if (ssid.length() == 0 || ssid.length() > 32) {
    renderForm("Network name is required (32 characters max).");
    return;
  }
  if (password.length() > 64) {
    renderForm("Password is too long.");
    return;
  }
  Link::Endpoint endpoint{};
  if (!Link::normalizeServer(host.c_str(), port.c_str(), tls,
                             path.length() > 0 ? path.c_str() : Link::kDefaultPath,
                             &endpoint)) {
    renderForm("Server host or port is not valid.");
    return;
  }
  if (path.length() > 0 && path.charAt(0) != '/' &&
      host.indexOf("://") < 0) {
    renderForm("Path has to start with /.");
    return;
  }
  if (gallery.length() > 0) {
    Link::Endpoint galleryEndpoint{};
    if (gallery.length() >= sizeof(next.galleryUrl) ||
        !Link::parseUrl(gallery.c_str(), true, 443, "/api/upload", &galleryEndpoint)) {
      renderForm("Gallery URL is not valid.");
      return;
    }
  }
  if (clearToken) {
    next.galleryToken[0] = '\0';
  } else if (token.length() > 0) {
    if (!Link::headerValueSafe(token.c_str())) {
      renderForm("Gallery token has a character that cannot go in a header.");
      return;
    }
    if (!copyArg(next.galleryToken, sizeof(next.galleryToken), token)) {
      renderForm("Gallery token is too long.");
      return;
    }
  }
  if (!copyArg(next.ssid, sizeof(next.ssid), ssid) ||
      !copyArg(next.password, sizeof(next.password), password) ||
      !copyArg(next.galleryUrl, sizeof(next.galleryUrl), gallery)) {
    renderForm("One of those fields is too long.");
    return;
  }
  memcpy(next.host, endpoint.host, sizeof(next.host));
  next.port = endpoint.port;
  next.tls = endpoint.tls;
  memcpy(next.path, endpoint.path, sizeof(next.path));
  if (!NetConfig::save(next)) {
    renderForm("Could not store settings.");
    return;
  }
  Console::printf("WIFI saved ssid='%s' host=%s:%u gallery=%s\n", next.ssid, next.host,
                  next.port, next.galleryUrl[0] != '\0' ? "yes" : "no");
  gWeb.sendHeader("Cache-Control", "no-store");
  gWeb.send(200, "text/html",
            "<!doctype html><html><body style=\"font-family:sans-serif;"
            "background:#140c12;color:#ff69b4;padding:24px\">"
            "<h1>Saved</h1><p>Somnium is joining Wi-Fi. You can close this page.</p>"
            "</body></html>");
  gWeb.client().flush();
  delay(300);
  gPhase = Link::Phase::Portal;
  // Applied below by the caller path: stop the portal and join.
  stopPortal();
  gFailures = 0;
  startJoin();
  gPhase = Link::Phase::Associating;
  LinkStatus::set(Link::headerLabel(gPhase));
  Console::printf("LINK %s\n", Link::headerLabel(gPhase));
}

void registerRoutes() {
  gWeb.on("/", HTTP_GET, handleRoot);
  gWeb.on("/save", HTTP_POST, handleSave);
  const char *captive[] = {"/generate_204",
                           "/gen_204",
                           "/hotspot-detect.html",
                           "/canonical.html",
                           "/success.txt",
                           "/connecttest.txt",
                           "/ncsi.txt",
                           "/fwlink",
                           "/redirect",
                           "/library/test/success.html"};
  for (const char *path : captive) {
    gWeb.on(path, HTTP_ANY, redirectPortal);
  }
  gWeb.onNotFound(redirectPortal);
}

void startPortal() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  if (!WiFi.softAP(Link::kPortalSsid)) {
    Console::println("WIFI portal failed");
    gPortalUp = false;
    return;
  }
  const IPAddress ip = WiFi.softAPIP();
  if (gPortalUp) {
    gDns.stop();
  }
  gDns.start(53, "*", ip);
  if (!gRoutesReady) {
    registerRoutes();
    gRoutesReady = true;
  }
  if (!gPortalUp) {
    gWeb.begin();
    gPortalUp = true;
  }
  Console::printf("WIFI portal %s http://%s/\n", Link::kPortalSsid, ip.toString().c_str());
}

void apply(Link::Transition next) {
  if (next.stopPortal) {
    stopPortal();
  }
  if (next.startPortal) {
    startPortal();
  }
  if (next.startJoin) {
    if (gPhase != Link::Phase::Backoff) {
      gFailures = 0;
    }
    startJoin();
  }
  gPhase = next.phase;
  if (gPhase == Link::Phase::Online) {
    gFailures = 0;
    startMdns();
    TimeSync::begin();
    Console::printf("WIFI %s %d dBm\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
  }
  LinkStatus::set(Link::headerLabel(gPhase));
  Console::printf("LINK %s\n", Link::headerLabel(gPhase));
}

}  // namespace

void WifiLink::begin() {
  NetConfig::load();
  WiFi.persistent(false);
  WiFi.setSleep(false);
  WiFi.setHostname("somnium-cam");
  gPhase = Link::Phase::Backoff;
  gFailures = 0;
  const Link::Event event =
      NetConfig::hasWifi() ? Link::Event::BootHasCreds : Link::Event::BootNoCreds;
  apply(Link::transition(gPhase, event));
}

void WifiLink::poll() {
  if (gBusy) {
    return;
  }
  if (gPortalRequested) {
    gPortalRequested = false;
    apply(Link::transition(gPhase, Link::Event::PortalRequested));
  }
  switch (gPhase) {
    case Link::Phase::Portal:
      if (!gPortalUp && millis() - gPortalRetryMs >= kPortalRetryMs) {
        gPortalRetryMs = millis();
        startPortal();
      }
      if (gPortalUp) {
        gDns.processNextRequest();
        gWeb.handleClient();
      }
      break;
    case Link::Phase::Associating:
      if (WiFi.status() == WL_CONNECTED) {
        apply(Link::transition(gPhase, Link::Event::Associated));
      } else if (millis() - gJoinStartedMs >= kJoinTimeoutMs) {
        gFailures += 1;
        if (!gDidScan) {
          gDidScan = true;
          scanNearby();
        }
        gNextJoinMs = millis() + Link::joinBackoffMs(gFailures);
        apply(Link::transition(gPhase, Link::Event::JoinTimeout));
      }
      break;
    case Link::Phase::Backoff:
      if (WiFi.status() == WL_CONNECTED) {
        apply(Link::transition(gPhase, Link::Event::Associated));
      } else if (static_cast<long>(millis() - gNextJoinMs) >= 0) {
        apply(Link::transition(gPhase, Link::Event::BackoffElapsed));
      }
      break;
    case Link::Phase::Online:
      if (WiFi.status() != WL_CONNECTED) {
        gFailures += 1;
        gNextJoinMs = millis() + Link::joinBackoffMs(gFailures);
        apply(Link::transition(gPhase, Link::Event::LinkLost));
      }
      break;
  }
}

void WifiLink::reload() {
  NetConfig::load();
  const Link::Event event =
      NetConfig::hasWifi() ? Link::Event::ConfigHasCreds : Link::Event::ConfigNoCreds;
  apply(Link::transition(gPhase, event));
}

void WifiLink::requestPortal() { gPortalRequested = true; }

void WifiLink::setBusy(bool busy) { gBusy = busy; }

bool WifiLink::isOnline() { return WiFi.status() == WL_CONNECTED; }

Link::Phase WifiLink::phase() { return gPhase; }

const char *WifiLink::headerLabel() { return Link::headerLabel(gPhase); }

void WifiLink::printStatus() {
  const NetSettings &settings = NetConfig::get();
  Serial.printf(
      "LINK phase=%s ip=%s rssi=%d host=%s:%u path=%s tls=%d ssid=%s gallery=%s\n",
      Link::headerLabel(gPhase), WiFi.localIP().toString().c_str(),
      isOnline() ? WiFi.RSSI() : 0, settings.host, settings.port, settings.path,
      settings.tls ? 1 : 0, settings.ssid, settings.galleryUrl[0] != '\0' ? "yes" : "no");
}

bool WifiLink::resolveServer(IPAddress *ip, bool *haveIp) {
  if (haveIp != nullptr) {
    *haveIp = false;
  }
  if (ip == nullptr || haveIp == nullptr) {
    return false;
  }
  const NetSettings &settings = NetConfig::get();
  if (!Link::isMdnsHost(settings.host)) {
    return true;
  }
  startMdns();
  char name[sizeof(settings.host)];
  const size_t length = strlen(settings.host);
  if (length < 6 || length >= sizeof(name)) {
    return false;
  }
  memcpy(name, settings.host, length - 6);
  name[length - 6] = '\0';
  const IPAddress found = MDNS.queryHost(name, 2000);
  if (found == IPAddress(0, 0, 0, 0)) {
    return false;
  }
  *ip = found;
  *haveIp = true;
  Console::printf("MDNS %s -> %s\n", settings.host, found.toString().c_str());
  return true;
}

}  // namespace Somnium
