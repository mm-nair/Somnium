#include "HttpFormat.h"

#include <stdio.h>
#include <string.h>

namespace Somnium {
namespace {

bool containsBytes(const uint8_t *data, size_t length, const char *needle) {
  const size_t needleLen = strlen(needle);
  if (needleLen == 0 || data == nullptr || length < needleLen) {
    return false;
  }
  for (size_t i = 0; i + needleLen <= length; ++i) {
    if (memcmp(data + i, needle, needleLen) == 0) {
      return true;
    }
  }
  return false;
}

bool quotedToken(const char *text) {
  if (text == nullptr || text[0] == '\0') {
    return false;
  }
  for (const unsigned char *p = reinterpret_cast<const unsigned char *>(text); *p != 0;
       ++p) {
    if (*p < 0x20 || *p > 0x7e || *p == '"' || *p == '\\') {
      return false;
    }
  }
  return true;
}

bool textFieldOk(const uint8_t *data, size_t length) {
  if (data == nullptr) {
    return length == 0;
  }
  for (size_t i = 0; i < length; ++i) {
    if (data[i] == '\r' || data[i] == '\n') {
      return false;
    }
  }
  return true;
}

bool pickBoundary(char *out, size_t outLen, const HttpPart *parts, size_t count) {
  for (unsigned n = 0; n < 8; ++n) {
    const int wrote = snprintf(out, outLen, "----SomniumCook%08x", n);
    if (wrote <= 0 || static_cast<size_t>(wrote) >= outLen) {
      return false;
    }
    bool clash = false;
    for (size_t i = 0; i < count; ++i) {
      if (containsBytes(parts[i].data, parts[i].length, out)) {
        clash = true;
        break;
      }
    }
    if (!clash) {
      return true;
    }
  }
  return false;
}

int partPrefix(char *buf, size_t cap, const char *boundary, const HttpPart &part) {
  if (part.filename == nullptr) {
    return snprintf(buf, cap,
                    "--%s\r\n"
                    "Content-Disposition: form-data; name=\"%s\"\r\n"
                    "\r\n",
                    boundary, part.name);
  }
  const char *type =
      part.contentType != nullptr ? part.contentType : "application/octet-stream";
  return snprintf(buf, cap,
                  "--%s\r\n"
                  "Content-Disposition: form-data; name=\"%s\"; filename=\"%s\"\r\n"
                  "Content-Type: %s\r\n"
                  "\r\n",
                  boundary, part.name, part.filename, type);
}

bool writeAll(HttpWriteFn write, void *ctx, const void *data, size_t length) {
  if (length == 0) {
    return true;
  }
  return write(ctx, reinterpret_cast<const uint8_t *>(data), length);
}

bool partsOk(const HttpPart *parts, size_t count) {
  if (parts == nullptr && count > 0) {
    return false;
  }
  for (size_t i = 0; i < count; ++i) {
    if (!quotedToken(parts[i].name)) {
      return false;
    }
    if (parts[i].filename != nullptr && !quotedToken(parts[i].filename)) {
      return false;
    }
    if (parts[i].contentType != nullptr && !quotedToken(parts[i].contentType)) {
      return false;
    }
    if (parts[i].length > 0 && parts[i].data == nullptr) {
      return false;
    }
    if (parts[i].filename == nullptr && !textFieldOk(parts[i].data, parts[i].length)) {
      return false;
    }
  }
  return true;
}

}  // namespace

bool httpTokenHeaderSafe(const char *token) {
  if (token == nullptr || token[0] == '\0') {
    return false;
  }
  size_t n = 0;
  for (const unsigned char *p = reinterpret_cast<const unsigned char *>(token); *p != 0;
       ++p) {
    if (*p < 0x20 || *p > 0x7e) {
      return false;
    }
    ++n;
    if (n > 200) {
      return false;
    }
  }
  return true;
}

bool httpWriteRequest(const HttpTarget &target, const HttpPart *parts, size_t count,
                      HttpWriteFn write, void *ctx) {
  if (write == nullptr || target.host == nullptr || target.host[0] == '\0' ||
      target.path == nullptr || target.path[0] != '/' || target.port <= 0 ||
      target.port > 65535 || count == 0 || !partsOk(parts, count)) {
    return false;
  }
  const char *token = target.token;
  if (token != nullptr && token[0] != '\0' && !httpTokenHeaderSafe(token)) {
    return false;
  }

  char boundary[40];
  if (!pickBoundary(boundary, sizeof(boundary), parts, count)) {
    return false;
  }

  char prefix[320];
  size_t bodyLen = 0;
  for (size_t i = 0; i < count; ++i) {
    const int headerLen = partPrefix(prefix, sizeof(prefix), boundary, parts[i]);
    if (headerLen <= 0 || static_cast<size_t>(headerLen) >= sizeof(prefix)) {
      return false;
    }
    bodyLen += static_cast<size_t>(headerLen) + parts[i].length + 2;
  }
  char epilogue[48];
  const int epilogueLen = snprintf(epilogue, sizeof(epilogue), "--%s--\r\n", boundary);
  if (epilogueLen <= 0 || static_cast<size_t>(epilogueLen) >= sizeof(epilogue)) {
    return false;
  }
  bodyLen += static_cast<size_t>(epilogueLen);

  const bool defaultPort =
      (target.tls && target.port == 443) || (!target.tls && target.port == 80);
  char hostLine[192];
  int hostLen = 0;
  if (defaultPort) {
    hostLen = snprintf(hostLine, sizeof(hostLine), "Host: %s\r\n", target.host);
  } else {
    hostLen = snprintf(hostLine, sizeof(hostLine), "Host: %s:%d\r\n", target.host,
                       target.port);
  }
  if (hostLen <= 0 || static_cast<size_t>(hostLen) >= sizeof(hostLine)) {
    return false;
  }

  char tokenLine[240];
  tokenLine[0] = '\0';
  if (token != nullptr && token[0] != '\0') {
    const int tokenLen =
        snprintf(tokenLine, sizeof(tokenLine), "X-Somnium-Token: %s\r\n", token);
    if (tokenLen <= 0 || static_cast<size_t>(tokenLen) >= sizeof(tokenLine)) {
      return false;
    }
  }

  char headers[640];
  const int headerLen = snprintf(
      headers, sizeof(headers),
      "POST %s HTTP/1.1\r\n"
      "%s"
      "%s"
      "Content-Type: multipart/form-data; boundary=%s\r\n"
      "Content-Length: %lu\r\n"
      "Connection: close\r\n"
      "Accept: image/jpeg\r\n"
      "\r\n",
      target.path, hostLine, tokenLine, boundary, static_cast<unsigned long>(bodyLen));
  if (headerLen <= 0 || static_cast<size_t>(headerLen) >= sizeof(headers)) {
    return false;
  }
  if (!writeAll(write, ctx, headers, static_cast<size_t>(headerLen))) {
    return false;
  }

  for (size_t i = 0; i < count; ++i) {
    const int oneLen = partPrefix(prefix, sizeof(prefix), boundary, parts[i]);
    if (oneLen <= 0 || !writeAll(write, ctx, prefix, static_cast<size_t>(oneLen)) ||
        !writeAll(write, ctx, parts[i].data, parts[i].length) ||
        !writeAll(write, ctx, "\r\n", 2)) {
      return false;
    }
  }
  return writeAll(write, ctx, epilogue, static_cast<size_t>(epilogueLen));
}

}  // namespace Somnium
