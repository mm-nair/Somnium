#include "NetConfig.h"

#include <Preferences.h>
#include <stdio.h>
#include <string.h>

#include "LinkPlan.h"

#if __has_include(<wifi_credentials.h>)
#include <wifi_credentials.h>
#endif
#if __has_include(<server_config.h>)
#include <server_config.h>
#endif

#ifndef SOMNIUM_WIFI_SSID
#define SOMNIUM_WIFI_SSID ""
#endif
#ifndef SOMNIUM_WIFI_PASSWORD
#define SOMNIUM_WIFI_PASSWORD ""
#endif
#ifndef SOMNIUM_SERVER_HOST
#define SOMNIUM_SERVER_HOST "somnium.local"
#endif
#ifndef SOMNIUM_SERVER_PORT
#define SOMNIUM_SERVER_PORT 8080
#endif
#ifndef SOMNIUM_SERVER_PATH
#define SOMNIUM_SERVER_PATH "/v1/process"
#endif
#ifndef SOMNIUM_SERVER_TLS
#define SOMNIUM_SERVER_TLS 0
#endif
#ifndef SOMNIUM_GALLERY_URL
#define SOMNIUM_GALLERY_URL ""
#endif
#ifndef SOMNIUM_GALLERY_TOKEN
#define SOMNIUM_GALLERY_TOKEN ""
#endif

namespace Somnium {
namespace {

constexpr char kNamespace[] = "somnium";
NetSettings gSettings{};

void copyField(char *dst, size_t capacity, const char *src) {
  if (capacity == 0) {
    return;
  }
  if (src == nullptr) {
    dst[0] = '\0';
    return;
  }
  size_t index = 0;
  for (; index + 1 < capacity && src[index] != '\0'; ++index) {
    dst[index] = src[index];
  }
  dst[index] = '\0';
}

void copyString(char *dst, size_t capacity, const String &src) {
  copyField(dst, capacity, src.c_str());
}

void loadCompileTime(NetSettings *settings) {
  memset(settings, 0, sizeof(*settings));
  copyField(settings->ssid, sizeof(settings->ssid), SOMNIUM_WIFI_SSID);
  copyField(settings->password, sizeof(settings->password), SOMNIUM_WIFI_PASSWORD);
  char portText[8];
  snprintf(portText, sizeof(portText), "%d", SOMNIUM_SERVER_PORT);
  Link::Endpoint endpoint{};
  if (!Link::normalizeServer(SOMNIUM_SERVER_HOST, portText, SOMNIUM_SERVER_TLS != 0,
                             SOMNIUM_SERVER_PATH, &endpoint)) {
    Link::normalizeServer("somnium.local", "8080", false, Link::kDefaultPath, &endpoint);
  }
  copyField(settings->host, sizeof(settings->host), endpoint.host);
  settings->port = endpoint.port;
  settings->tls = endpoint.tls;
  copyField(settings->path, sizeof(settings->path), endpoint.path);
  copyField(settings->galleryUrl, sizeof(settings->galleryUrl), SOMNIUM_GALLERY_URL);
  copyField(settings->galleryToken, sizeof(settings->galleryToken), SOMNIUM_GALLERY_TOKEN);
  settings->provisioned = false;
}

void loadNvs(NetSettings *settings) {
  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) {
    return;
  }
  const bool provisioned = prefs.getBool("prov", false);
  if (!provisioned) {
    prefs.end();
    return;
  }
  copyString(settings->ssid, sizeof(settings->ssid), prefs.getString("ssid", ""));
  copyString(settings->password, sizeof(settings->password), prefs.getString("pass", ""));
  copyString(settings->host, sizeof(settings->host), prefs.getString("host", ""));
  const uint32_t port = prefs.getUInt("port", settings->port);
  if (port > 0 && port <= 65535) {
    settings->port = static_cast<uint16_t>(port);
  }
  settings->tls = prefs.getBool("tls", settings->tls);
  copyString(settings->path, sizeof(settings->path), prefs.getString("path", settings->path));
  if (settings->path[0] == '\0') {
    copyField(settings->path, sizeof(settings->path), Link::kDefaultPath);
  }
  copyString(settings->galleryUrl, sizeof(settings->galleryUrl), prefs.getString("gurl", ""));
  copyString(settings->galleryToken, sizeof(settings->galleryToken),
             prefs.getString("gtok", ""));
  if (settings->host[0] == '\0') {
    copyField(settings->host, sizeof(settings->host), "somnium.local");
  }
  settings->provisioned = true;
  prefs.end();
}

}  // namespace

void NetConfig::load() {
  loadCompileTime(&gSettings);
  loadNvs(&gSettings);
}

bool NetConfig::save(const NetSettings &settings) {
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) {
    return false;
  }
  prefs.putString("ssid", settings.ssid);
  prefs.putString("pass", settings.password);
  prefs.putString("host", settings.host);
  prefs.putUInt("port", settings.port);
  prefs.putBool("tls", settings.tls);
  prefs.putString("path", settings.path);
  prefs.putString("gurl", settings.galleryUrl);
  prefs.putString("gtok", settings.galleryToken);
  prefs.putBool("prov", true);
  prefs.end();
  gSettings = settings;
  gSettings.provisioned = true;
  return true;
}

void NetConfig::clear() {
  Preferences prefs;
  if (prefs.begin(kNamespace, false)) {
    prefs.clear();
    prefs.end();
  }
  load();
}

const NetSettings &NetConfig::get() { return gSettings; }

bool NetConfig::hasWifi() { return gSettings.ssid[0] != '\0'; }

}  // namespace Somnium
