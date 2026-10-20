#include "HttpCook.h"

#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <esp_rom_crc.h>

#include "Config.h"
#include "Console.h"
#include "Image.h"
#include "LinkPlan.h"
#include "NetConfig.h"
#include "TimeSync.h"
#include "WifiLink.h"

namespace Somnium {
namespace {

HttpCook::PumpFn gPump = nullptr;
HttpCook::NoteFn gNote = nullptr;
unsigned long gLastPumpMs = 0;
bool gLoggedInsecureTls = false;

struct Exchange {
  int status = 0;
  bool timedOut = false;
  bool fatal = false;
  uint8_t *body = nullptr;
  size_t length = 0;
  char error[48] = "";
};

void note(const char *text) {
  if (gNote != nullptr && text != nullptr) {
    gNote(text);
  }
}

void maybePump(bool enabled) {
  if (!enabled) {
    yield();
    return;
  }
  const unsigned long now = millis();
  if (gPump != nullptr && now - gLastPumpMs >= 100) {
    gLastPumpMs = now;
    gPump();
  } else {
    yield();
  }
}

void setError(Exchange *result, const char *text) {
  snprintf(result->error, sizeof(result->error), "%s", text != nullptr ? text : "http");
}

void *psAlloc(size_t bytes) {
  void *buffer = ps_malloc(bytes);
  if (buffer == nullptr) {
    buffer = malloc(bytes);
  }
  return buffer;
}

bool containsBytes(const uint8_t *data, size_t length, const char *needle) {
  const size_t needleLen = strlen(needle);
  if (needleLen == 0 || length < needleLen) {
    return false;
  }
  for (size_t index = 0; index + needleLen <= length; ++index) {
    if (memcmp(data + index, needle, needleLen) == 0) {
      return true;
    }
  }
  return false;
}

const char *pickBoundary(const uint8_t *jpeg, size_t jpegLen) {
  static const char *options[] = {Link::kMultipartBoundary, "SomniumBoundary8e4b",
                                  "SomniumBoundary9c5c"};
  for (const char *option : options) {
    if (!containsBytes(jpeg, jpegLen, option)) {
      return option;
    }
  }
  return options[0];
}

bool writeAll(WiFiClient &client, const uint8_t *data, size_t length,
              unsigned long started, uint32_t timeoutMs) {
  size_t sent = 0;
  while (sent < length) {
    if (millis() - started >= timeoutMs) {
      return false;
    }
    size_t chunk = length - sent;
    if (chunk > 1024) {
      chunk = 1024;
    }
    const int wrote = client.write(data + sent, chunk);
    if (wrote <= 0) {
      if (!client.connected()) {
        return false;
      }
      delay(1);
      continue;
    }
    sent += static_cast<size_t>(wrote);
    yield();
  }
  return true;
}

int headerInt(const char *headers, const char *name) {
  const size_t nameLen = strlen(name);
  const char *cursor = headers;
  while (*cursor != '\0') {
    if (strncasecmp(cursor, name, nameLen) == 0) {
      return atoi(cursor + nameLen);
    }
    const char *newline = strchr(cursor, '\n');
    if (newline == nullptr) {
      break;
    }
    cursor = newline + 1;
  }
  return -1;
}

bool headerHasToken(const char *headers, const char *name, const char *token) {
  const size_t nameLen = strlen(name);
  const size_t tokenLen = strlen(token);
  const char *cursor = headers;
  while (*cursor != '\0') {
    if (strncasecmp(cursor, name, nameLen) == 0) {
      const char *lineEnd = strchr(cursor, '\n');
      const size_t lineLen =
          lineEnd != nullptr ? static_cast<size_t>(lineEnd - cursor) : strlen(cursor);
      for (size_t index = 0; index + tokenLen <= lineLen; ++index) {
        if (strncasecmp(cursor + index, token, tokenLen) == 0) {
          return true;
        }
      }
      return false;
    }
    const char *newline = strchr(cursor, '\n');
    if (newline == nullptr) {
      break;
    }
    cursor = newline + 1;
  }
  return false;
}

bool readHeaders(WiFiClient &client, char *headers, size_t capacity, unsigned long started,
                 uint32_t timeoutMs, bool pump, bool *timedOut) {
  size_t used = 0;
  while (millis() - started < timeoutMs) {
    if (client.available() <= 0) {
      if (!client.connected()) {
        break;
      }
      maybePump(pump);
      delay(1);
      continue;
    }
    const int value = client.read();
    if (value < 0) {
      break;
    }
    if (used + 1 >= capacity) {
      return false;
    }
    headers[used++] = static_cast<char>(value);
    headers[used] = '\0';
    if (used >= 4 && memcmp(headers + used - 4, "\r\n\r\n", 4) == 0) {
      return true;
    }
  }
  *timedOut = millis() - started >= timeoutMs;
  return false;
}

bool readBody(WiFiClient &client, uint8_t *buffer, size_t length, unsigned long started,
              uint32_t timeoutMs) {
  size_t got = 0;
  while (got < length && millis() - started < timeoutMs) {
    if (client.available() <= 0) {
      if (!client.connected()) {
        break;
      }
      delay(1);
      continue;
    }
    const int chunk = client.read(buffer + got, length - got);
    if (chunk <= 0) {
      delay(1);
      continue;
    }
    got += static_cast<size_t>(chunk);
  }
  return got == length;
}

bool exchange(WiFiClient &client, const char *host, uint16_t port, const char *path,
              const char *method, const char *extraHeaders, const uint8_t *request,
              size_t requestLen, uint32_t timeoutMs, bool pumpHeaders, size_t maxBody,
              bool requireLength, Exchange *result) {
  *result = Exchange{};
  const unsigned long started = millis();
  char header[768];
  const int headerLen = snprintf(
      header, sizeof(header),
      "%s %s HTTP/1.1\r\nHost: %s:%u\r\n%sContent-Length: %u\r\nConnection: close\r\n\r\n",
      method, path != nullptr && path[0] != '\0' ? path : "/", host,
      static_cast<unsigned>(port), extraHeaders != nullptr ? extraHeaders : "",
      static_cast<unsigned>(requestLen));
  if (headerLen <= 0 || static_cast<size_t>(headerLen) >= sizeof(header)) {
    setError(result, "headers");
    result->fatal = true;
    return false;
  }
  if (!writeAll(client, reinterpret_cast<const uint8_t *>(header),
                static_cast<size_t>(headerLen), started, timeoutMs) ||
      (requestLen > 0 && !writeAll(client, request, requestLen, started, timeoutMs))) {
    result->timedOut = millis() - started >= timeoutMs;
    setError(result, result->timedOut ? "timeout" : "connect");
    return false;
  }
  client.flush();

  char responseHeader[1280];
  if (!readHeaders(client, responseHeader, sizeof(responseHeader), started, timeoutMs,
                   pumpHeaders, &result->timedOut)) {
    setError(result, result->timedOut ? "timeout" : "headers");
    result->fatal = !result->timedOut && responseHeader[0] == '\0';
    return false;
  }
  const char *space = strchr(responseHeader, ' ');
  if (space == nullptr) {
    setError(result, "status");
    result->fatal = true;
    return false;
  }
  result->status = atoi(space + 1);
  if (headerHasToken(responseHeader, "transfer-encoding:", "chunked")) {
    setError(result, "chunked");
    result->fatal = true;
    return false;
  }
  const int declared = headerInt(responseHeader, "content-length:");
  size_t length = 0;
  if (declared >= 0) {
    length = static_cast<size_t>(declared);
  } else if (!requireLength) {
    length = maxBody;
  } else {
    setError(result, "no length");
    result->fatal = true;
    return false;
  }
  if (length > maxBody) {
    setError(result, "too large");
    result->fatal = true;
    client.stop();
    return false;
  }
  if (length == 0) {
    return true;
  }
  uint8_t *body = static_cast<uint8_t *>(psAlloc(length + 1));
  if (body == nullptr) {
    setError(result, "oom");
    result->fatal = true;
    return false;
  }
  if (declared >= 0) {
    if (!readBody(client, body, length, started, timeoutMs)) {
      free(body);
      result->timedOut = millis() - started >= timeoutMs;
      setError(result, result->timedOut ? "timeout" : "short body");
      return false;
    }
  } else {
    size_t got = 0;
    unsigned long idleStart = millis();
    while (got < length && millis() - started < timeoutMs) {
      if (client.available() <= 0) {
        if (!client.connected() || millis() - idleStart > 1500) {
          break;
        }
        delay(1);
        continue;
      }
      idleStart = millis();
      const int chunk = client.read(body + got, length - got);
      if (chunk > 0) {
        got += static_cast<size_t>(chunk);
      }
    }
    length = got;
  }
  body[length] = '\0';
  result->body = body;
  result->length = length;
  return true;
}

bool dial(WiFiClient &client, bool haveIp, const IPAddress &ip, const char *host,
          uint16_t port, uint32_t timeoutMs) {
  const int32_t timeout = static_cast<int32_t>(timeoutMs);
  if (haveIp) {
    return client.connect(ip, port, timeout) > 0;
  }
  return client.connect(host, port, timeout) > 0;
}

void prepareTls(WiFiClientSecure *client, uint32_t timeoutMs) {
  // The laptop server is plain HTTP. Gallery HTTPS does not pin a CA yet.
  client->setInsecure();
  client->setHandshakeTimeout(timeoutMs > 15000 ? 15000 : timeoutMs);
  if (!gLoggedInsecureTls) {
    gLoggedInsecureTls = true;
    Console::println("TLS certificate check off");
  }
}

bool talk(const Link::Endpoint &endpoint, bool haveIp, const IPAddress &ip,
          const char *method, const char *extraHeaders, const uint8_t *request,
          size_t requestLen, uint32_t timeoutMs, bool pumpHeaders, size_t maxBody,
          bool requireLength, Exchange *result) {
  if (endpoint.tls) {
    WiFiClientSecure secure;
    prepareTls(&secure, timeoutMs);
    if (!dial(secure, haveIp, ip, endpoint.host, endpoint.port, Link::kConnectTimeoutMs)) {
      setError(result, "connect");
      return false;
    }
    return exchange(secure, endpoint.host, endpoint.port, endpoint.path, method, extraHeaders,
                    request, requestLen, timeoutMs, pumpHeaders, maxBody, requireLength,
                    result);
  }
  WiFiClient plain;
  plain.setTimeout(timeoutMs / 1000 + 1);
  if (!dial(plain, haveIp, ip, endpoint.host, endpoint.port, Link::kConnectTimeoutMs)) {
    setError(result, "connect");
    return false;
  }
  return exchange(plain, endpoint.host, endpoint.port, endpoint.path, method, extraHeaders,
                  request, requestLen, timeoutMs, pumpHeaders, maxBody, requireLength,
                  result);
}

void logHttpBody(const Exchange &result) {
  if (result.body == nullptr || result.length == 0) {
    return;
  }
  char snippet[80];
  size_t out = 0;
  const size_t limit = result.length < 60 ? result.length : 60;
  for (size_t index = 0; index < limit && out + 1 < sizeof(snippet); ++index) {
    const unsigned char value = result.body[index];
    snippet[out++] = (value >= 32 && value < 127) ? static_cast<char>(value) : ' ';
  }
  snippet[out] = '\0';
  Console::printf("HTTP %d %s\n", result.status, snippet);
}

bool serverHealthy(const NetSettings &settings, bool haveIp, const IPAddress &ip,
                   Exchange *result) {
  Link::Endpoint endpoint{};
  memset(&endpoint, 0, sizeof(endpoint));
  memcpy(endpoint.host, settings.host, sizeof(endpoint.host));
  endpoint.port = settings.port;
  endpoint.tls = settings.tls;
  snprintf(endpoint.path, sizeof(endpoint.path), "/health");
  const bool ok = talk(endpoint, haveIp, ip, "GET", nullptr, nullptr, 0,
                       Link::kHealthTimeoutMs, true, 512, false, result);
  if (!ok) {
    return false;
  }
  if (result->status != 200 || result->body == nullptr ||
      strstr(reinterpret_cast<char *>(result->body), "ok") == nullptr) {
    setError(result, "bad health");
    result->fatal = result->status > 0 && result->status != 408 && result->status != 429 &&
                    result->status < 500;
    logHttpBody(*result);
    return false;
  }
  return true;
}

bool resolve(bool *haveIp, IPAddress *ip, Exchange *result) {
  *haveIp = false;
  if (!Link::isMdnsHost(NetConfig::get().host)) {
    return true;
  }
  if (!WifiLink::resolveServer(ip, haveIp) || !*haveIp) {
    setError(result, "mdns");
    return false;
  }
  return true;
}

void styleToken(char *dst, size_t capacity, const char *styleId) {
  if (dst == nullptr || capacity == 0) {
    return;
  }
  const char *source = styleId != nullptr && styleId[0] != '\0' ? styleId : "cartoon";
  size_t index = 0;
  for (; source[index] != '\0' && index + 1 < capacity; ++index) {
    const unsigned char value = static_cast<unsigned char>(source[index]);
    const bool ok = (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
                    (value >= '0' && value <= '9') || value == '_' || value == '-';
    if (!ok) {
      snprintf(dst, capacity, "style");
      return;
    }
    dst[index] = static_cast<char>(value);
  }
  dst[index] = '\0';
  if (dst[0] == '\0') {
    snprintf(dst, capacity, "style");
  }
}

bool uploadGallery(const NetSettings &settings, const uint8_t *jpeg, size_t jpegLen,
                   const uint8_t *dream, size_t dreamLen, const char *styleId) {
  if (settings.galleryUrl[0] == '\0' || !Link::headerValueSafe(settings.galleryToken)) {
    return true;
  }
  Link::Endpoint endpoint{};
  if (!Link::parseUrl(settings.galleryUrl, true, 443, "/api/upload", &endpoint)) {
    Console::println("GALLERY bad url");
    return false;
  }
  char headers[360];
  if (!Link::formatGalleryHeaders(headers, sizeof(headers), settings.galleryToken)) {
    Console::println("GALLERY token rejected");
    return false;
  }
  char meta[160];
  char style[24];
  styleToken(style, sizeof(style), styleId);
  snprintf(meta, sizeof(meta), "{\"style_id\":\"%s\",\"source\":\"wifi\"}", style);
  const Link::FilePart files[] = {
      {"orig", "capture_0_orig.jpg", "image/jpeg", jpeg, jpegLen},
      {"dream", "capture_1_dream.jpg", "image/jpeg", dream, dreamLen},
      {"meta", "capture.json", "application/json",
       reinterpret_cast<const uint8_t *>(meta), strlen(meta)},
  };
  const char *boundary = pickBoundary(jpeg, jpegLen);
  size_t bodyLen = 0;
  if (!Link::buildMultipart(nullptr, 0, &bodyLen, boundary, nullptr, 0, files, 3)) {
    Console::println("GALLERY body");
    return false;
  }
  uint8_t *body = static_cast<uint8_t *>(psAlloc(bodyLen));
  size_t written = 0;
  if (body == nullptr ||
      !Link::buildMultipart(body, bodyLen, &written, boundary, nullptr, 0, files, 3)) {
    free(body);
    Console::println("GALLERY oom");
    return false;
  }
  char typed[480];
  const int typedLen =
      snprintf(typed, sizeof(typed),
               "Content-Type: multipart/form-data; boundary=%s\r\n%s", boundary, headers);
  if (typedLen <= 0 || static_cast<size_t>(typedLen) >= sizeof(typed)) {
    free(body);
    Console::println("GALLERY headers");
    return false;
  }
  bool uploaded = false;
  for (int attempt = 0; attempt < Link::kMaxGalleryAttempts; ++attempt) {
    Exchange result;
    const IPAddress unused;
    const bool sent =
        talk(endpoint, false, unused, "POST", typed, body, written, Link::kGalleryTimeoutMs,
             false, 512, false, &result);
    const bool httpOk = sent && result.status >= 200 && result.status < 300;
    if (!httpOk) {
      Console::printf("GALLERY %s\n", result.error[0] != '\0' ? result.error : "fail");
      if (result.status > 0) {
        Console::printf("GALLERY http %d\n", result.status);
      }
    }
    free(result.body);
    if (httpOk) {
      uploaded = true;
      break;
    }
    if (result.fatal ||
        !Link::shouldRetry(Link::classifyHttp(result.status, result.timedOut), attempt,
                           Link::kMaxGalleryAttempts)) {
      break;
    }
    delay(Link::retryDelayMs(attempt));
  }
  free(body);
  if (uploaded) {
    Console::println("GALLERY ok");
  } else {
    Console::println("GALLERY failed");
  }
  return uploaded;
}

}  // namespace

void HttpCook::setPump(PumpFn fn) { gPump = fn; }

void HttpCook::setNote(NoteFn fn) { gNote = fn; }

bool HttpCook::process(const uint8_t *jpeg, size_t jpegLen, const char *styleId,
                       const char *takenAt, float lat, float lon, JpegBuffer &outDream,
                       String *err) {
  outDream = JpegBuffer{};
  if (jpeg == nullptr || jpegLen == 0) {
    if (err != nullptr) {
      *err = "empty";
    }
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    if (err != nullptr) {
      *err = "wifi";
    }
    return false;
  }

  const NetSettings &settings = NetConfig::get();
  Exchange failure;
  bool haveIp = false;
  IPAddress ip;
  bool healthy = false;
  for (int attempt = 0; attempt < Link::kMaxHealthAttempts; ++attempt) {
    char status[40];
    Link::formatCookStatus(status, sizeof(status), attempt + 1);
    note(status);
    failure = Exchange{};
    if (!resolve(&haveIp, &ip, &failure)) {
      // mDNS can miss a packet; retry, then fall back to USB.
    } else if (serverHealthy(settings, haveIp, ip, &failure)) {
      healthy = true;
      free(failure.body);
      failure.body = nullptr;
      break;
    }
    free(failure.body);
    failure.body = nullptr;
    if (failure.fatal ||
        !Link::shouldRetry(failure.timedOut ? Link::FailClass::Timeout
                                            : Link::classifyHttp(failure.status, false),
                           attempt, Link::kMaxHealthAttempts)) {
      break;
    }
    delay(Link::retryDelayMs(attempt));
  }
  if (!healthy) {
    if (err != nullptr) {
      *err = failure.error[0] != '\0' ? failure.error : "server unreachable";
    }
    Console::printf("HTTP health failed %s\n", err != nullptr ? err->c_str() : "");
    return false;
  }

  char style[24];
  char taken[32];
  char latText[16];
  char lonText[16];
  styleToken(style, sizeof(style), styleId);
  const char *takenValue = nullptr;
  if (takenAt != nullptr && takenAt[0] != '\0') {
    snprintf(taken, sizeof(taken), "%s", takenAt);
    takenValue = taken;
  } else if (TimeSync::formatUtc(taken, sizeof(taken))) {
    takenValue = taken;
  }
  Link::TextPart texts[8];
  int textCount = 0;
  texts[textCount++] = Link::TextPart{"style", style};
  if (takenValue != nullptr) {
    texts[textCount++] = Link::TextPart{"taken_at", takenValue};
  }
  if (!isnan(lat) && !isnan(lon)) {
    snprintf(latText, sizeof(latText), "%.5f", lat);
    snprintf(lonText, sizeof(lonText), "%.5f", lon);
    texts[textCount++] = Link::TextPart{"lat", latText};
    texts[textCount++] = Link::TextPart{"lon", lonText};
  }
  texts[textCount++] = Link::TextPart{"source", "wifi"};
  const bool cameraGallery =
      settings.galleryUrl[0] != '\0' && Link::headerValueSafe(settings.galleryToken);
  if (cameraGallery) {
    texts[textCount++] = Link::TextPart{"skip_gallery", "1"};
  }
  const Link::FilePart files[] = {
      {"image", "capture.jpg", "image/jpeg", jpeg, jpegLen},
  };
  const char *boundary = pickBoundary(jpeg, jpegLen);
  size_t bodyLen = 0;
  if (!Link::buildMultipart(nullptr, 0, &bodyLen, boundary, texts, textCount, files, 1)) {
    if (err != nullptr) {
      *err = "body";
    }
    return false;
  }
  uint8_t *body = static_cast<uint8_t *>(psAlloc(bodyLen));
  size_t written = 0;
  if (body == nullptr ||
      !Link::buildMultipart(body, bodyLen, &written, boundary, texts, textCount, files, 1)) {
    free(body);
    if (err != nullptr) {
      *err = "oom";
    }
    return false;
  }

  Link::Endpoint endpoint{};
  memset(&endpoint, 0, sizeof(endpoint));
  memcpy(endpoint.host, settings.host, sizeof(endpoint.host));
  endpoint.port = settings.port;
  endpoint.tls = settings.tls;
  snprintf(endpoint.path, sizeof(endpoint.path), "%s",
           settings.path[0] != '\0' ? settings.path : Link::kDefaultPath);

  char contentType[120];
  snprintf(contentType, sizeof(contentType),
           "Content-Type: multipart/form-data; boundary=%s\r\nAccept: image/jpeg\r\n",
           boundary);

  bool cooked = false;
  Exchange result;
  for (int attempt = 0; attempt < Link::kMaxHttpAttempts; ++attempt) {
    char status[40];
    Link::formatCookStatus(status, sizeof(status), attempt + 1);
    note(status);
    result = Exchange{};
    if (!resolve(&haveIp, &ip, &result)) {
      // counted below
    } else {
      Console::printf("HTTP POST http%s://%s:%u%s (%u B)\n", endpoint.tls ? "s" : "",
                      endpoint.host, endpoint.port, endpoint.path,
                      static_cast<unsigned>(written));
      const bool sent =
          talk(endpoint, haveIp, ip, "POST", contentType, body, written,
               Defaults::kCookTimeoutMs, true, Link::kMaxDreamBytes, true, &result);
      if (sent && result.status == 200 &&
          Image::looksLikeJpeg(result.body, result.length)) {
        cooked = true;
        break;
      }
      if (sent && result.status == 200) {
        setError(&result, "bad jpeg");
      } else if (result.status > 0 && result.error[0] == '\0') {
        snprintf(result.error, sizeof(result.error), "http %d", result.status);
        logHttpBody(result);
      }
    }
    Console::printf("HTTP attempt %d %s\n", attempt + 1,
                    result.error[0] != '\0' ? result.error : "fail");
    free(result.body);
    result.body = nullptr;
    const Link::FailClass fail =
        result.fatal ? Link::FailClass::HttpFatal
                     : Link::classifyHttp(result.status, result.timedOut || result.status == 0);
    if (!Link::shouldRetry(fail, attempt, Link::kMaxHttpAttempts)) {
      break;
    }
    delay(Link::retryDelayMs(attempt));
  }
  free(body);
  if (!cooked) {
    free(result.body);
    if (err != nullptr) {
      *err = result.error[0] != '\0' ? result.error : "http";
    }
    return false;
  }

  outDream.data = result.body;
  outDream.length = result.length;
  outDream.crc32 = esp_rom_crc32_le(0, outDream.data, outDream.length);
  result.body = nullptr;
  Console::printf("HTTP DREAM %u B\n", static_cast<unsigned>(outDream.length));
  if (cameraGallery) {
    note("gallery...");
    uploadGallery(settings, jpeg, jpegLen, outDream.data, outDream.length, styleId);
  }
  return true;
}

}  // namespace Somnium
