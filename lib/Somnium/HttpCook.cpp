#include "HttpFormat.h"
#include "Host.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>

#include <esp_rom_crc.h>
#include <esp_system.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Config.h"
#include "Console.h"
#include "Image.h"

#if __has_include("server_config.h")
#include "server_config.h"
#endif

#ifndef SOMNIUM_SERVER_HOST
#define SOMNIUM_SERVER_HOST ""
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
#ifndef SOMNIUM_PROCESS_TOKEN
#define SOMNIUM_PROCESS_TOKEN ""
#endif
#ifndef SOMNIUM_GALLERY_URL
#define SOMNIUM_GALLERY_URL ""
#endif
#ifndef SOMNIUM_GALLERY_TOKEN
#define SOMNIUM_GALLERY_TOKEN ""
#endif

namespace Somnium {
namespace {

struct ParsedUrl {
  bool tls;
  char host[96];
  int port;
  char path[128];
};

struct Net {
  WiFiClient plain;
  WiFiClientSecure secure;
  WiFiClient *active;
};

bool parseHttpUrl(const char *url, ParsedUrl &out) {
  if (url == nullptr) {
    return false;
  }
  const char *rest = nullptr;
  if (strncmp(url, "https://", 8) == 0) {
    out.tls = true;
    out.port = 443;
    rest = url + 8;
  } else if (strncmp(url, "http://", 7) == 0) {
    out.tls = false;
    out.port = 80;
    rest = url + 7;
  } else {
    return false;
  }
  if (rest[0] == '\0' || rest[0] == '/') {
    return false;
  }
  const char *slash = strchr(rest, '/');
  const char *hostEnd = slash != nullptr ? slash : rest + strlen(rest);
  const char *colon = nullptr;
  for (const char *p = rest; p < hostEnd; ++p) {
    if (*p == ':') {
      colon = p;
    }
  }
  const size_t hostLen = static_cast<size_t>((colon != nullptr ? colon : hostEnd) - rest);
  if (hostLen == 0 || hostLen >= sizeof(out.host)) {
    return false;
  }
  memcpy(out.host, rest, hostLen);
  out.host[hostLen] = '\0';
  if (colon != nullptr) {
    if (colon + 1 >= hostEnd) {
      return false;
    }
    int port = 0;
    for (const char *p = colon + 1; p < hostEnd; ++p) {
      if (*p < '0' || *p > '9') {
        return false;
      }
      port = port * 10 + (*p - '0');
      if (port > 65535) {
        return false;
      }
    }
    if (port <= 0) {
      return false;
    }
    out.port = port;
  }
  if (slash == nullptr) {
    memcpy(out.path, "/", 2);
  } else if (strlen(slash) >= sizeof(out.path)) {
    return false;
  } else {
    memcpy(out.path, slash, strlen(slash) + 1);
  }
  return true;
}

bool connectTo(Net &net, const char *host, int port, bool tls) {
  net.active = nullptr;
  if (tls) {
    // Public hosts (Fly) present a normal web certificate. The board does
    // not ship a CA store; setInsecure still encrypts the POST.
    net.secure.setInsecure();
    net.secure.setHandshakeTimeout(20000);
    net.secure.setTimeout(15);
    net.active = &net.secure;
  } else {
    net.plain.setTimeout(15);
    net.active = &net.plain;
  }
  const int ok = net.active->connect(host, static_cast<uint16_t>(port), 20000);
  return ok == 1;
}

bool writeAll(WiFiClient &client, const uint8_t *data, size_t length) {
  size_t sent = 0;
  while (sent < length) {
    const size_t n = client.write(data + sent, length - sent);
    if (n == 0) {
      return false;
    }
    sent += n;
    yield();
  }
  return true;
}

bool writeThunk(void *ctx, const uint8_t *data, size_t length) {
  return writeAll(*static_cast<WiFiClient *>(ctx), data, length);
}

void setErr(String *err, const char *message) {
  if (err != nullptr) {
    *err = message;
  }
}

bool readHeaders(WiFiClient &client, int &status, size_t &contentLength, bool &haveLength,
                 uint32_t timeoutMs, void (*pump)()) {
  char line[384];
  size_t n = 0;
  bool first = true;
  haveLength = false;
  contentLength = 0;
  status = 0;
  const unsigned long started = millis();
  unsigned long lastPump = started;
  while (millis() - started < timeoutMs) {
    if (client.available() <= 0) {
      if (!client.connected()) {
        return false;
      }
      const unsigned long now = millis();
      if (pump != nullptr && now - lastPump >= 100) {
        lastPump = now;
        pump();
      } else {
        yield();
      }
      delay(1);
      continue;
    }
    const int raw = client.read();
    if (raw < 0) {
      continue;
    }
    if (raw == '\r') {
      continue;
    }
    if (raw != '\n') {
      if (n + 1 < sizeof(line)) {
        line[n++] = static_cast<char>(raw);
      }
      continue;
    }
    line[n] = '\0';
    n = 0;
    if (first) {
      first = false;
      if (strncmp(line, "HTTP/1.", 7) != 0) {
        return false;
      }
      const char *space = strchr(line, ' ');
      if (space == nullptr) {
        return false;
      }
      status = atoi(space + 1);
      continue;
    }
    if (line[0] == '\0') {
      return true;
    }
    char *colon = strchr(line, ':');
    if (colon == nullptr) {
      continue;
    }
    *colon = '\0';
    for (char *p = line; *p != '\0'; ++p) {
      if (*p >= 'A' && *p <= 'Z') {
        *p = static_cast<char>(*p - 'A' + 'a');
      }
    }
    char *value = colon + 1;
    while (*value == ' ' || *value == '\t') {
      ++value;
    }
    if (strcmp(line, "content-length") == 0) {
      contentLength = static_cast<size_t>(strtoul(value, nullptr, 10));
      haveLength = true;
    }
  }
  return false;
}

bool readExact(WiFiClient &client, uint8_t *dst, size_t length, uint32_t timeoutMs) {
  size_t got = 0;
  const unsigned long started = millis();
  while (got < length && millis() - started < timeoutMs) {
    if (client.available() <= 0) {
      if (!client.connected()) {
        break;
      }
      yield();
      delay(1);
      continue;
    }
    const int n = client.read(dst + got, length - got);
    if (n <= 0) {
      yield();
      continue;
    }
    got += static_cast<size_t>(n);
  }
  return got == length;
}

bool exchange(Net &net, const HttpTarget &target, const HttpPart *parts, size_t count,
              bool wantBody, size_t maxBody, uint32_t timeoutMs, void (*pump)(),
              uint8_t *&body, size_t &bodyLen, int &status, const char *&err) {
  body = nullptr;
  bodyLen = 0;
  status = 0;
  err = "link write";
  if (!connectTo(net, target.host, target.port, target.tls != 0)) {
    err = "link write";
    return false;
  }
  if (!httpWriteRequest(target, parts, count, writeThunk, net.active)) {
    net.active->stop();
    err = "link write";
    return false;
  }
  net.active->flush();

  size_t contentLength = 0;
  bool haveLength = false;
  if (!readHeaders(*net.active, status, contentLength, haveLength, timeoutMs, pump)) {
    net.active->stop();
    err = "http timeout";
    return false;
  }
  if (wantBody && status != 200) {
    net.active->stop();
    err = status == 401 ? "unauthorized" : "http status";
    return false;
  }
  if (!haveLength) {
    net.active->stop();
    if (!wantBody && status >= 200 && status < 300) {
      err = nullptr;
      return true;
    }
    err = "no length";
    return false;
  }
  if (!wantBody) {
    size_t left = contentLength;
    uint8_t sink[128];
    size_t dumped = 0;
    while (left > 0 && dumped < 1024) {
      const size_t chunk = left < sizeof(sink) ? left : sizeof(sink);
      if (!readExact(*net.active, sink, chunk, timeoutMs)) {
        break;
      }
      left -= chunk;
      dumped += chunk;
    }
    net.active->stop();
    if (status < 200 || status >= 300) {
      err = "gallery http";
      return false;
    }
    err = nullptr;
    return true;
  }
  if (contentLength < 4 || contentLength > maxBody) {
    net.active->stop();
    err = "dream length";
    return false;
  }
  body = static_cast<uint8_t *>(ps_malloc(contentLength));
  if (body == nullptr) {
    body = static_cast<uint8_t *>(malloc(contentLength));
  }
  if (body == nullptr) {
    net.active->stop();
    err = "oom dream";
    return false;
  }
  if (!readExact(*net.active, body, contentLength, timeoutMs)) {
    free(body);
    body = nullptr;
    net.active->stop();
    err = "http short";
    return false;
  }
  bodyLen = contentLength;
  net.active->stop();
  if (status == 401) {
    free(body);
    body = nullptr;
    err = "unauthorized";
    return false;
  }
  if (status != 200) {
    free(body);
    body = nullptr;
    err = "http status";
    return false;
  }
  err = nullptr;
  return true;
}

bool styleToken(const char *style, char *out, size_t outLen) {
  size_t n = 0;
  if (style != nullptr) {
    for (const char *p = style; *p != '\0' && n + 1 < outLen; ++p) {
      const unsigned char c = static_cast<unsigned char>(*p);
      if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
          c == '_' || c == '-') {
        out[n++] = static_cast<char>(c);
      }
    }
  }
  if (n == 0) {
    if (outLen < 5) {
      return false;
    }
    memcpy(out, "shot", 5);
    return true;
  }
  out[n] = '\0';
  return true;
}

bool galleryReady(ParsedUrl &url) {
  if (SOMNIUM_GALLERY_URL[0] == '\0') {
    return false;
  }
  if (!httpTokenHeaderSafe(SOMNIUM_GALLERY_TOKEN)) {
    if (SOMNIUM_GALLERY_TOKEN[0] != '\0') {
      Console::printf("GALLERY token rejected\n");
    }
    return false;
  }
  if (!parseHttpUrl(SOMNIUM_GALLERY_URL, url)) {
    Console::printf("GALLERY bad url\n");
    return false;
  }
  return true;
}

void uploadGallery(const uint8_t *jpeg, size_t jpegLen, const uint8_t *dream, size_t dreamLen,
                   const char *styleId, const ParsedUrl &url, void (*pump)()) {
  char style[32];
  char origName[40];
  char dreamName[40];
  char metaName[40];
  char meta[256];
  if (!styleToken(styleId, style, sizeof(style))) {
    Console::printf("GALLERY skip\n");
    return;
  }
  const unsigned id = esp_random();
  snprintf(origName, sizeof(origName), "w%08x_0_orig.jpg", id);
  snprintf(dreamName, sizeof(dreamName), "w%08x_1_dream.jpg", id);
  snprintf(metaName, sizeof(metaName), "w%08x.json", id);
  const int metaLen =
      snprintf(meta, sizeof(meta),
               "{\"style_id\":\"%s\",\"files\":{\"orig\":\"%s\",\"dream\":\"%s\"}}", style,
               origName, dreamName);
  if (metaLen <= 0 || static_cast<size_t>(metaLen) >= sizeof(meta)) {
    Console::printf("GALLERY skip\n");
    return;
  }

  HttpPart parts[3];
  parts[0] = {"orig", origName, "image/jpeg", jpeg, jpegLen};
  parts[1] = {"dream", dreamName, "image/jpeg", dream, dreamLen};
  parts[2] = {"meta", metaName, "application/json", reinterpret_cast<const uint8_t *>(meta),
              static_cast<size_t>(metaLen)};
  HttpTarget target{url.host, url.port, url.tls ? 1 : 0, url.path, SOMNIUM_GALLERY_TOKEN};

  Net net;
  uint8_t *body = nullptr;
  size_t bodyLen = 0;
  int status = 0;
  const char *error = nullptr;
  const bool ok = exchange(net, target, parts, 3, false, 0, 20000, pump, body, bodyLen,
                           status, error);
  free(body);
  if (!ok) {
    Console::printf("GALLERY fail %d\n", status);
    return;
  }
  Console::printf("GALLERY ok\n");
}

bool httpConfigured() { return SOMNIUM_SERVER_HOST[0] != '\0'; }

bool httpCook(const uint8_t *jpeg, size_t jpegLen, const char *styleId, const char *takenAt,
              float lat, float lon, JpegBuffer &outDream, String *err, void (*pump)()) {
  outDream = JpegBuffer{};
  if (WiFi.status() != WL_CONNECTED) {
    setErr(err, "wifi down");
    return false;
  }
  if (SOMNIUM_SERVER_PORT <= 0 || SOMNIUM_SERVER_PORT > 65535 ||
      SOMNIUM_SERVER_PATH[0] != '/') {
    setErr(err, "bad server");
    return false;
  }
  if (SOMNIUM_PROCESS_TOKEN[0] != '\0' && !httpTokenHeaderSafe(SOMNIUM_PROCESS_TOKEN)) {
    setErr(err, "bad process token");
    return false;
  }
  const char *style = (styleId != nullptr && styleId[0] != '\0') ? styleId : "cartoon";
  for (const char *p = style; *p != '\0'; ++p) {
    if (*p == '\r' || *p == '\n') {
      setErr(err, "bad style");
      return false;
    }
  }

  ParsedUrl galleryUrl{};
  const bool skipGallery = galleryReady(galleryUrl);

  char takenBuf[48];
  char latBuf[24];
  char lonBuf[24];
  const bool hasTaken = takenAt != nullptr && takenAt[0] != '\0';
  if (hasTaken) {
    snprintf(takenBuf, sizeof(takenBuf), "%s", takenAt);
    for (char *p = takenBuf; *p != '\0'; ++p) {
      if (*p == '\r' || *p == '\n') {
        setErr(err, "bad time");
        return false;
      }
    }
  }
  const bool hasPlace = !isnan(lat) && !isnan(lon);
  if (hasPlace) {
    snprintf(latBuf, sizeof(latBuf), "%.5f", lat);
    snprintf(lonBuf, sizeof(lonBuf), "%.5f", lon);
  }

  HttpPart parts[6];
  size_t n = 0;
  parts[n++] = {"style", nullptr, nullptr, reinterpret_cast<const uint8_t *>(style),
                strlen(style)};
  if (hasTaken) {
    parts[n++] = {"taken_at", nullptr, nullptr, reinterpret_cast<const uint8_t *>(takenBuf),
                  strlen(takenBuf)};
  }
  if (hasPlace) {
    parts[n++] = {"lat", nullptr, nullptr, reinterpret_cast<const uint8_t *>(latBuf),
                  strlen(latBuf)};
    parts[n++] = {"lon", nullptr, nullptr, reinterpret_cast<const uint8_t *>(lonBuf),
                  strlen(lonBuf)};
  }
  if (skipGallery) {
    parts[n++] = {"skip_gallery", nullptr, nullptr,
                  reinterpret_cast<const uint8_t *>("1"), 1};
  }
  parts[n++] = {"image", "capture.jpg", "image/jpeg", jpeg, jpegLen};

  const char *token = SOMNIUM_PROCESS_TOKEN[0] != '\0' ? SOMNIUM_PROCESS_TOKEN : nullptr;
  HttpTarget target{SOMNIUM_SERVER_HOST, SOMNIUM_SERVER_PORT, SOMNIUM_SERVER_TLS,
                    SOMNIUM_SERVER_PATH, token};
  Console::printf("HOST cook %s %s:%d\n", SOMNIUM_SERVER_TLS ? "tls" : "http",
                  SOMNIUM_SERVER_HOST, SOMNIUM_SERVER_PORT);

  Net net;
  uint8_t *body = nullptr;
  size_t bodyLen = 0;
  int status = 0;
  const char *error = nullptr;
  const bool ok = exchange(net, target, parts, n, true, 400000, Defaults::kCookTimeoutMs,
                           pump, body, bodyLen, status, error);
  if (!ok) {
    char statusBuf[24];
    const char *message = error != nullptr ? error : "http";
    if (status == 401) {
      message = "unauthorized";
    } else if (status >= 400) {
      snprintf(statusBuf, sizeof(statusBuf), "http %d", status);
      message = statusBuf;
    }
    setErr(err, message);
    Console::printf("HOST http fail %s\n", message);
    return false;
  }
  if (!Image::looksLikeJpeg(body, bodyLen)) {
    free(body);
    setErr(err, "bad jpeg");
    return false;
  }
  outDream.data = body;
  outDream.length = bodyLen;
  outDream.crc32 = esp_rom_crc32_le(0, body, bodyLen);
  Console::printf("HOST DREAM %u B\n", static_cast<unsigned>(bodyLen));
  if (skipGallery) {
    uploadGallery(jpeg, jpegLen, body, bodyLen, style, galleryUrl, pump);
  }
  return true;
}

struct BindHttp {
  BindHttp() {
    HttpOps ops;
    ops.configured = httpConfigured;
    ops.cook = httpCook;
    httpBind(ops);
    appDirectCookRegister(httpConfigured);
  }
};

BindHttp gBindHttp __attribute__((used));

}  // namespace

void httpAnchor() {}

}  // namespace Somnium
