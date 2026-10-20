#pragma once

#include <stddef.h>
#include <stdint.h>

namespace Somnium {

// One multipart part. filename == nullptr is a text field; otherwise a file.
// data may be null only when length is 0.
struct HttpPart {
  const char *name;
  const char *filename;
  const char *contentType;
  const uint8_t *data;
  size_t length;
};

struct HttpTarget {
  const char *host;
  int port;
  int tls;
  const char *path;
  const char *token;
};

// Printable ASCII, 1..200 characters, no CR or LF. Empty is not safe.
bool httpTokenHeaderSafe(const char *token);

typedef bool (*HttpWriteFn)(void *ctx, const uint8_t *data, size_t len);

// Write one HTTP/1.1 multipart POST. A non-empty token is sent as
// X-Somnium-Token. An empty or null token omits that header. Returns false
// before any write when the token or a part is not header-safe.
bool httpWriteRequest(const HttpTarget &target, const HttpPart *parts, size_t count,
                      HttpWriteFn write, void *ctx);

}  // namespace Somnium
