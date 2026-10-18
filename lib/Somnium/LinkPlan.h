#pragma once

#include <stddef.h>
#include <stdint.h>

namespace Somnium {
namespace Link {

// Pure decisions for the Wi-Fi link. No sockets, so host tests can compile this
// without the ESP32 Arduino core. WifiLink applies the transitions.

constexpr int kMaxHttpAttempts = 4;
constexpr int kMaxHealthAttempts = 3;
constexpr int kMaxGalleryAttempts = 2;
constexpr uint32_t kConnectTimeoutMs = 8000;
constexpr uint32_t kHealthTimeoutMs = 4000;
constexpr uint32_t kGalleryTimeoutMs = 20000;
constexpr uint32_t kAssocWaitMs = 3000;
constexpr size_t kMaxDreamBytes = 400000;

constexpr char kPortalSsid[] = "Somnium-Setup";
constexpr char kMdnsHost[] = "somnium";
constexpr char kDefaultPath[] = "/v1/process";
constexpr char kMultipartBoundary[] = "SomniumBoundary7f3a";

enum class Phase : uint8_t { Portal, Associating, Online, Backoff };

enum class Event : uint8_t {
  BootNoCreds,
  BootHasCreds,
  Associated,
  JoinTimeout,
  LinkLost,
  PortalRequested,
  PortalSaved,
  BackoffElapsed,
  ConfigHasCreds,
  ConfigNoCreds,
};

enum class FailClass : uint8_t { Connect, Timeout, HttpRetry, HttpFatal };

struct Transition {
  Phase phase;
  bool startPortal;
  bool stopPortal;
  bool startJoin;
};

struct Endpoint {
  bool tls;
  bool mdns;
  bool portExplicit;
  char host[80];
  uint16_t port;
  char path[64];
};

struct TextPart {
  const char *name;
  const char *value;
};

struct FilePart {
  const char *name;
  const char *filename;
  const char *type;
  const uint8_t *data;
  size_t length;
};

const char *headerLabel(Phase phase);
bool isMdnsHost(const char *host);
void formatCookStatus(char *buf, size_t length, int attempt);
uint32_t joinBackoffMs(int failedJoins);
uint32_t retryDelayMs(int attemptIndex);
FailClass classifyHttp(int status, bool timedOut);
bool shouldRetry(FailClass fail, int attemptIndex, int maxAttempts);
bool headerValueSafe(const char *value);
bool formatGalleryHeaders(char *dst, size_t capacity, const char *token);
bool parseUrl(const char *url, bool defaultTls, uint16_t defaultPort,
              const char *defaultPath, Endpoint *out);
bool normalizeServer(const char *hostField, const char *portField, bool tlsChecked,
                     const char *defaultPath, Endpoint *out);
bool buildMultipart(uint8_t *dst, size_t capacity, size_t *written,
                    const char *boundary, const TextPart *text, int textCount,
                    const FilePart *files, int fileCount);
Transition transition(Phase phase, Event event);

}  // namespace Link
}  // namespace Somnium
