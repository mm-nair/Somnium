#include "LinkPlan.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

namespace Somnium {
namespace Link {
namespace {

struct Writer {
  uint8_t *dst;
  size_t cap;
  size_t used;
  bool overflow;
};

void writeBytes(Writer *writer, const void *data, size_t length) {
  if (writer->overflow) {
    return;
  }
  if (writer->used > SIZE_MAX - length) {
    writer->overflow = true;
    return;
  }
  if (writer->dst != nullptr) {
    if (writer->used + length > writer->cap) {
      writer->overflow = true;
      return;
    }
    memcpy(writer->dst + writer->used, data, length);
  }
  writer->used += length;
}

void writeStr(Writer *writer, const char *text) {
  if (text == nullptr) {
    writer->overflow = true;
    return;
  }
  writeBytes(writer, text, strlen(text));
}

bool tokenCharOk(char character) {
  const unsigned char value = static_cast<unsigned char>(character);
  if (value <= 0x20 || value > 0x7e) {
    return false;
  }
  return character != '"' && character != '\\';
}

bool fieldToken(const char *text) {
  if (text == nullptr || text[0] == '\0' || strlen(text) > 80) {
    return false;
  }
  for (const char *cursor = text; *cursor != '\0'; ++cursor) {
    if (*cursor == '"' || *cursor == '\r' || *cursor == '\n') {
      return false;
    }
  }
  return true;
}

bool copyText(char *dst, size_t capacity, const char *src) {
  if (dst == nullptr || capacity == 0 || src == nullptr) {
    return false;
  }
  const size_t length = strlen(src);
  if (length + 1 > capacity) {
    return false;
  }
  memcpy(dst, src, length + 1);
  return true;
}

void skipSpaces(const char **cursor) {
  while (**cursor == ' ' || **cursor == '\t') {
    *cursor += 1;
  }
}

bool endsWithLocal(const char *host) {
  if (host == nullptr) {
    return false;
  }
  const size_t length = strlen(host);
  if (length < 6) {
    return false;
  }
  const char *suffix = host + length - 6;
  return strncasecmp(suffix, ".local", 6) == 0;
}

bool hostOk(const char *host) {
  if (host == nullptr || host[0] == '\0' || strlen(host) >= 80) {
    return false;
  }
  if (host[0] == '.' || host[0] == '-') {
    return false;
  }
  for (const char *cursor = host; *cursor != '\0'; ++cursor) {
    const unsigned char value = static_cast<unsigned char>(*cursor);
    const bool ok = (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
                    (value >= '0' && value <= '9') || value == '.' || value == '-';
    if (!ok) {
      return false;
    }
  }
  return true;
}

bool parsePort(const char *text, uint16_t *port) {
  if (text == nullptr || text[0] == '\0') {
    return false;
  }
  unsigned long value = 0;
  for (const char *cursor = text; *cursor != '\0'; ++cursor) {
    if (*cursor < '0' || *cursor > '9') {
      return false;
    }
    value = value * 10UL + static_cast<unsigned long>(*cursor - '0');
    if (value > 65535UL) {
      return false;
    }
  }
  if (value == 0) {
    return false;
  }
  *port = static_cast<uint16_t>(value);
  return true;
}

bool applyPath(Endpoint *out, const char *path, const char *defaultPath) {
  if (path == nullptr || path[0] == '\0') {
    return copyText(out->path, sizeof(out->path),
                    defaultPath != nullptr && defaultPath[0] != '\0' ? defaultPath
                                                                     : kDefaultPath);
  }
  if (path[0] != '/' || strlen(path) >= sizeof(out->path)) {
    return false;
  }
  return copyText(out->path, sizeof(out->path), path);
}

}  // namespace

bool isMdnsHost(const char *host) { return endsWithLocal(host); }

const char *headerLabel(Phase phase) {
  switch (phase) {
    case Phase::Portal:
      return "Setup";
    case Phase::Online:
      return "WiFi*";
    case Phase::Associating:
    case Phase::Backoff:
      return "WiFi.";
  }
  return "WiFi.";
}

void formatCookStatus(char *buf, size_t length, int attempt) {
  if (buf == nullptr || length == 0) {
    return;
  }
  if (attempt <= 1) {
    snprintf(buf, length, "wifi > server...");
  } else {
    snprintf(buf, length, "wifi retry %d...", attempt);
  }
}

uint32_t joinBackoffMs(int failedJoins) {
  if (failedJoins < 1) {
    failedJoins = 1;
  }
  int shift = failedJoins - 1;
  if (shift > 5) {
    shift = 5;
  }
  const uint32_t delay = 1000u << shift;
  return delay > 30000u ? 30000u : delay;
}

uint32_t retryDelayMs(int attemptIndex) {
  if (attemptIndex < 0) {
    attemptIndex = 0;
  }
  if (attemptIndex > 4) {
    attemptIndex = 4;
  }
  const uint32_t delay = 500u << attemptIndex;
  return delay > 8000u ? 8000u : delay;
}

FailClass classifyHttp(int status, bool timedOut) {
  if (timedOut) {
    return FailClass::Timeout;
  }
  if (status <= 0) {
    return FailClass::Connect;
  }
  if (status == 408 || status == 429 || status == 500 || status == 502 ||
      status == 503 || status == 504) {
    return FailClass::HttpRetry;
  }
  return FailClass::HttpFatal;
}

bool shouldRetry(FailClass fail, int attemptIndex, int maxAttempts) {
  if (maxAttempts < 1 || attemptIndex + 1 >= maxAttempts) {
    return false;
  }
  return fail != FailClass::HttpFatal;
}

bool headerValueSafe(const char *value) {
  if (value == nullptr || value[0] == '\0' || strlen(value) > 127) {
    return false;
  }
  for (const char *cursor = value; *cursor != '\0'; ++cursor) {
    if (!tokenCharOk(*cursor)) {
      return false;
    }
  }
  return true;
}

bool formatGalleryHeaders(char *dst, size_t capacity, const char *token) {
  if (!headerValueSafe(token) || dst == nullptr || capacity < 64) {
    return false;
  }
  const int written =
      snprintf(dst, capacity, "Authorization: Bearer %s\r\nX-Somnium-Token: %s\r\n",
               token, token);
  return written > 0 && static_cast<size_t>(written) < capacity;
}

bool parseUrl(const char *url, bool defaultTls, uint16_t defaultPort,
              const char *defaultPath, Endpoint *out) {
  if (out == nullptr) {
    return false;
  }
  memset(out, 0, sizeof(*out));
  out->tls = defaultTls;
  out->port = defaultPort == 0 ? static_cast<uint16_t>(defaultTls ? 443 : 80) : defaultPort;
  if (!applyPath(out, nullptr, defaultPath)) {
    return false;
  }
  if (url == nullptr) {
    return false;
  }
  const char *cursor = url;
  skipSpaces(&cursor);
  if (*cursor == '\0') {
    return false;
  }

  if (strncasecmp(cursor, "https://", 8) == 0) {
    out->tls = true;
    out->port = 443;
    cursor += 8;
  } else if (strncasecmp(cursor, "http://", 7) == 0) {
    out->tls = false;
    out->port = 80;
    cursor += 7;
  }
  if (*cursor == '\0' || *cursor == '/' || *cursor == '@') {
    return false;
  }

  char host[sizeof(out->host)];
  size_t hostLen = 0;
  while (*cursor != '\0' && *cursor != '/' && *cursor != ':' && *cursor != '?' &&
         *cursor != '#' && *cursor != ' ') {
    if (hostLen + 1 >= sizeof(host)) {
      return false;
    }
    host[hostLen++] = *cursor++;
  }
  host[hostLen] = '\0';
  if (!hostOk(host) || !copyText(out->host, sizeof(out->host), host)) {
    return false;
  }
  out->mdns = endsWithLocal(out->host);

  if (*cursor == ':') {
    ++cursor;
    char portText[8];
    size_t portLen = 0;
    while (*cursor >= '0' && *cursor <= '9') {
      if (portLen + 1 >= sizeof(portText)) {
        return false;
      }
      portText[portLen++] = *cursor++;
    }
    portText[portLen] = '\0';
    if (!parsePort(portText, &out->port)) {
      return false;
    }
    out->portExplicit = true;
  }
  if (*cursor == '?' || *cursor == '#') {
    return false;
  }
  if (*cursor == '/') {
    char path[sizeof(out->path)];
    size_t pathLen = 0;
    while (*cursor != '\0' && *cursor != '?' && *cursor != '#' && *cursor != ' ') {
      if (pathLen + 1 >= sizeof(path)) {
        return false;
      }
      path[pathLen++] = *cursor++;
    }
    path[pathLen] = '\0';
    if (*cursor != '\0' || !applyPath(out, path, defaultPath)) {
      return false;
    }
  } else if (*cursor != '\0') {
    return false;
  }
  return out->host[0] != '\0';
}

bool normalizeServer(const char *hostField, const char *portField, bool tlsChecked,
                     const char *defaultPath, Endpoint *out) {
  if (out == nullptr) {
    return false;
  }
  const char *host = hostField != nullptr ? hostField : "";
  while (*host == ' ' || *host == '\t') {
    ++host;
  }
  const char *port = portField != nullptr ? portField : "";
  while (*port == ' ' || *port == '\t') {
    ++port;
  }

  uint16_t numericPort = 8080;
  const bool portGiven = port[0] != '\0';
  if (portGiven && !parsePort(port, &numericPort)) {
    return false;
  }

  const bool hasScheme =
      strncasecmp(host, "https://", 8) == 0 || strncasecmp(host, "http://", 7) == 0;
  if (host[0] == '\0') {
    memset(out, 0, sizeof(*out));
    if (!copyText(out->host, sizeof(out->host), "somnium.local") ||
        !applyPath(out, nullptr, defaultPath)) {
      return false;
    }
    out->port = portGiven ? numericPort : static_cast<uint16_t>(8080);
    out->tls = tlsChecked;
    out->mdns = true;
    return true;
  }
  if (hasScheme) {
    const bool https = strncasecmp(host, "https://", 8) == 0;
    if (!parseUrl(host, https, https ? 443 : 80, defaultPath, out)) {
      return false;
    }
    if (!out->portExplicit && portGiven) {
      out->port = numericPort;
    }
    return true;
  }
  if (!hostOk(host)) {
    return false;
  }
  memset(out, 0, sizeof(*out));
  if (!copyText(out->host, sizeof(out->host), host) ||
      !applyPath(out, nullptr, defaultPath)) {
    return false;
  }
  out->port = portGiven ? numericPort : static_cast<uint16_t>(8080);
  out->tls = tlsChecked;
  out->mdns = endsWithLocal(out->host);
  return true;
}

bool buildMultipart(uint8_t *dst, size_t capacity, size_t *written, const char *boundary,
                    const TextPart *text, int textCount, const FilePart *files,
                    int fileCount) {
  if (written == nullptr || !fieldToken(boundary)) {
    return false;
  }
  if ((textCount > 0 && text == nullptr) || (fileCount > 0 && files == nullptr)) {
    return false;
  }
  Writer writer{dst, capacity, 0, false};
  for (int index = 0; index < textCount; ++index) {
    if (!fieldToken(text[index].name) || text[index].value == nullptr ||
        strchr(text[index].value, '\r') != nullptr ||
        strchr(text[index].value, '\n') != nullptr) {
      return false;
    }
    writeStr(&writer, "--");
    writeStr(&writer, boundary);
    writeStr(&writer, "\r\nContent-Disposition: form-data; name=\"");
    writeStr(&writer, text[index].name);
    writeStr(&writer, "\"\r\n\r\n");
    writeStr(&writer, text[index].value);
    writeStr(&writer, "\r\n");
  }
  for (int index = 0; index < fileCount; ++index) {
    const FilePart &file = files[index];
    if (!fieldToken(file.name) || !fieldToken(file.filename) || !fieldToken(file.type)) {
      return false;
    }
    if (file.length > 0 && file.data == nullptr) {
      return false;
    }
    writeStr(&writer, "--");
    writeStr(&writer, boundary);
    writeStr(&writer, "\r\nContent-Disposition: form-data; name=\"");
    writeStr(&writer, file.name);
    writeStr(&writer, "\"; filename=\"");
    writeStr(&writer, file.filename);
    writeStr(&writer, "\"\r\nContent-Type: ");
    writeStr(&writer, file.type);
    writeStr(&writer, "\r\n\r\n");
    if (file.length > 0) {
      writeBytes(&writer, file.data, file.length);
    }
    writeStr(&writer, "\r\n");
  }
  writeStr(&writer, "--");
  writeStr(&writer, boundary);
  writeStr(&writer, "--\r\n");
  if (writer.overflow) {
    return false;
  }
  *written = writer.used;
  return true;
}

Transition transition(Phase phase, Event event) {
  Transition next{phase, false, false, false};
  switch (event) {
    case Event::BootNoCreds:
    case Event::ConfigNoCreds:
      next.phase = Phase::Portal;
      next.startPortal = true;
      break;
    case Event::BootHasCreds:
    case Event::ConfigHasCreds:
      next.phase = Phase::Associating;
      next.stopPortal = true;
      next.startJoin = true;
      break;
    case Event::PortalRequested:
      next.phase = Phase::Portal;
      next.startPortal = true;
      break;
    case Event::PortalSaved:
      next.phase = Phase::Associating;
      next.stopPortal = true;
      next.startJoin = true;
      break;
    case Event::Associated:
      if (phase == Phase::Associating || phase == Phase::Backoff) {
        next.phase = Phase::Online;
      }
      break;
    case Event::JoinTimeout:
      if (phase == Phase::Associating) {
        next.phase = Phase::Backoff;
      }
      break;
    case Event::BackoffElapsed:
      if (phase == Phase::Backoff) {
        next.phase = Phase::Associating;
        next.startJoin = true;
      }
      break;
    case Event::LinkLost:
      if (phase == Phase::Online) {
        next.phase = Phase::Backoff;
      }
      break;
  }
  return next;
}

}  // namespace Link
}  // namespace Somnium
