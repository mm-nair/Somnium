#include "Host.h"

#include <Arduino.h>
#include <esp_rom_crc.h>
#include <esp_system.h>
#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "Camera.h"
#include "Config.h"
#include "Console.h"
#include "Image.h"
#include "Link.h"
#include "Storage.h"

namespace Somnium {
namespace {

bool gCooking = false;
Host::PumpFn gPump = nullptr;

char gPollLine[96];
size_t gPollLen = 0;

void linkNopBegin() {}
void linkNopPoll() {}
void linkNopBusy(bool) {}
bool linkNopClaim(CookPort *) { return false; }
int linkNopRead() { return -1; }
void linkNopDrop() {}

bool httpNopConfigured() { return false; }
bool httpNopCook(const uint8_t *, size_t, const char *, const char *, float, float,
                 JpegBuffer &, String *err, void (*)()) {
  if (err != nullptr) {
    *err = "http off";
  }
  return false;
}

HttpOps &httpOps() {
  static HttpOps ops = {httpNopConfigured, httpNopCook};
  return ops;
}

void applyTime(unsigned long epoch);
void noteTime(const char *line);

// Function-local so Link.cpp can install ops from a global constructor
// without racing this default.
LinkOps &linkOps() {
  static LinkOps ops = {linkNopBegin, linkNopPoll, linkNopBusy,
                        linkNopClaim, linkNopRead, linkNopDrop};
  return ops;
}

void pump() {
  linkOps().poll();
  if (gPump != nullptr) {
    gPump();
  } else {
    yield();
  }
}

int serialAvailable(void *) { return Serial.available(); }
int serialRead(void *) { return Serial.read(); }
size_t serialReadBytes(void *, uint8_t *dst, size_t len) {
  return Serial.readBytes(dst, len);
}
size_t serialWrite(void *, const uint8_t *data, size_t len) {
  return Serial.write(data, len);
}
void serialFlush(void *) { Serial.flush(); }

CookPort serialPort() {
  CookPort port{};
  port.available = serialAvailable;
  port.read = serialRead;
  port.readBytes = serialReadBytes;
  port.write = serialWrite;
  port.flush = serialFlush;
  return port;
}

bool portWriteAll(const CookPort &port, const uint8_t *data, size_t len) {
  size_t sent = 0;
  while (sent < len) {
    const size_t n = port.write(port.ctx, data + sent, len - sent);
    if (n == 0) {
      return false;
    }
    sent += n;
  }
  return true;
}

bool portPrint(const CookPort &port, const char *text) {
  if (text == nullptr) {
    return true;
  }
  return portWriteAll(port, reinterpret_cast<const uint8_t *>(text), strlen(text));
}

bool portPrintf(const CookPort &port, const char *fmt, ...) {
  char buffer[192];
  va_list args;
  va_start(args, fmt);
  const int wrote = vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);
  if (wrote <= 0) {
    return false;
  }
  size_t len = static_cast<size_t>(wrote);
  if (len >= sizeof(buffer)) {
    len = sizeof(buffer) - 1;
  }
  return portWriteAll(port, reinterpret_cast<const uint8_t *>(buffer), len);
}

void drainRx(const CookPort &port) {
  char line[96];
  size_t n = 0;
  int guard = 0;
  while (port.available(port.ctx) > 0 && guard < 65536) {
    const int raw = port.read(port.ctx);
    if (raw < 0) {
      break;
    }
    ++guard;
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
    noteTime(line);
  }
}

void resetPollAssembler() {
  gPollLen = 0;
  gPollLine[0] = '\0';
}

void applyTime(unsigned long epoch) {
  if (epoch < 1700000000UL) {
    return;
  }
  setenv("TZ", "PST8PDT,M3.2.0/2,M11.1.0/2", 1);
  tzset();
  timeval tv{};
  tv.tv_sec = static_cast<time_t>(epoch);
  tv.tv_usec = 0;
  settimeofday(&tv, nullptr);
  if (!gCooking) {
    Console::printf("TIME synced %lu\n", epoch);
  }
}

void noteTime(const char *line) {
  unsigned long epoch = 0;
  if (sscanf(line, "TIME %lu", &epoch) == 1) {
    applyTime(epoch);
  }
}

void sendPhoto(JpegBuffer &photo) {
  Serial.printf("PHOTO %u %08x %lu %lu %d %d\n",
                static_cast<unsigned>(photo.length), photo.crc32, photo.captureMs,
                photo.encodeMs, photo.width, photo.height);
  Serial.write(photo.data, photo.length);
  Serial.print("\nPHOTO_END\n");
  Serial.flush();
}

bool isKnownCommand(const char *line) {
  if (line == nullptr || line[0] == '\0') {
    return false;
  }
  unsigned long epoch = 0;
  if (sscanf(line, "TIME %lu", &epoch) == 1) {
    return true;
  }
  if (strcmp(line, "hello") == 0 || strcmp(line, "capture") == 0 ||
      strcmp(line, "cooktest") == 0 || strcmp(line, "sdlist") == 0 ||
      strcmp(line, "sdpull") == 0 || strcmp(line, "sdwipe") == 0) {
    return true;
  }
  int width = 0;
  int quality = 0;
  if (sscanf(line, "capture %d %d", &width, &quality) == 2) {
    return true;
  }
  return false;
}

void handleLine(char *line) {
  unsigned long epoch = 0;
  if (sscanf(line, "TIME %lu", &epoch) == 1) {
    applyTime(epoch);
    return;
  }

  if (strcmp(line, "hello") == 0) {
    Serial.printf("OK somnium %lu\n",
                  static_cast<unsigned long>(Defaults::kSerialBaud));
    return;
  }

  int width = Defaults::kCameraWidth;
  int quality = Defaults::kJpegQuality;
  if (strcmp(line, "capture") == 0 ||
      sscanf(line, "capture %d %d", &width, &quality) == 2) {
    JpegBuffer photo;
    if (!Image::takePhoto(photo, width, quality)) {
      Console::println("ERR capture");
      return;
    }
    sendPhoto(photo);
    Image::freePhoto(photo);
    return;
  }

  if (strcmp(line, "cooktest") == 0) {
    JpegBuffer photo;
    if (!Image::takePhoto(photo)) {
      Console::println("ERR capture");
      return;
    }
    JpegBuffer dream{};
    String err;
    const bool ok =
        Host::cook(photo.data, photo.length, "anime", nullptr,
                   Defaults::kVenueLat, Defaults::kVenueLon, dream, &err);
    Image::freePhoto(photo);
    if (!ok) {
      Console::printf("ERR cooktest %s\n", err.c_str());
      return;
    }
    Console::printf("OK cooktest %u\n", static_cast<unsigned>(dream.length));
    free(dream.data);
    return;
  }

  if (strcmp(line, "sdlist") == 0) {
    if (!Storage::ready()) {
      Console::println("ERR sd");
      return;
    }
    const int n = Storage::photoCount();
    Serial.printf("SDLIST %d\n", n);
    char path[64];
    for (int i = 0; i < n; ++i) {
      if (Storage::photoPathAt(i, path, sizeof(path))) {
        Serial.printf("%s\n", path);
      }
    }
    Serial.print("SDLIST_END\n");
    Serial.flush();
    return;
  }

  if (strcmp(line, "sdpull") == 0) {
    if (!Storage::ready()) {
      Console::println("ERR sd");
      return;
    }
    const int n = Storage::photoCount();
    Serial.printf("SDPULL %d\n", n);
    Serial.flush();
    char path[64];
    for (int i = 0; i < n; ++i) {
      if (!Storage::photoPathAt(i, path, sizeof(path))) {
        continue;
      }
      uint8_t *data = nullptr;
      size_t len = 0;
      if (!Storage::loadJpeg(path, &data, &len)) {
        Serial.printf("ERR file %s\n", path);
        continue;
      }
      const char *base = path;
      if (const char *slash = strrchr(path, '/')) {
        base = slash + 1;
      }
      Serial.printf("FILE %s %u\n", base, static_cast<unsigned>(len));
      Serial.write(data, len);
      Serial.print("\nFILE_END\n");
      Serial.flush();
      Storage::freeJpeg(data);
    }
    Serial.print("SDPULL_END\n");
    Serial.flush();
    return;
  }

  if (strcmp(line, "sdwipe") == 0) {
    if (!Storage::ready()) {
      Console::println("ERR sd");
      return;
    }
    Storage::clearGallery();
    Serial.print("OK sdwipe\n");
    Serial.flush();
    return;
  }
}

bool readExact(const CookPort &port, uint8_t *dst, size_t len, uint32_t timeoutMs) {
  size_t got = 0;
  const unsigned long started = millis();
  while (got < len && millis() - started < timeoutMs) {
    if (port.available(port.ctx) <= 0) {
      yield();
      delay(1);
      continue;
    }
    const size_t available = static_cast<size_t>(port.available(port.ctx));
    const size_t remaining = len - got;
    got += port.readBytes(port.ctx, dst + got,
                          available < remaining ? available : remaining);
  }
  return got == len;
}

bool waitDreamHeader(const CookPort &port, unsigned requestId, unsigned &dreamLen,
                     unsigned &dreamCrc, String *err) {
  char line[96];
  size_t n = 0;
  bool overflow = false;
  const unsigned long started = millis();
  unsigned long lastPump = started;

  while (millis() - started < Defaults::kCookTimeoutMs) {
    if (port.available(port.ctx) <= 0) {
      const unsigned long now = millis();
      if (now - lastPump >= 100) {
        lastPump = now;
        pump();
      } else {
        yield();
      }
      delay(1);
      continue;
    }

    const char c = static_cast<char>(port.read(port.ctx));
    if (c == '\r') {
      continue;
    }
    if (c != '\n') {
      if (n + 1 < sizeof(line)) {
        line[n++] = c;
      } else {
        overflow = true;
      }
      continue;
    }

    line[n] = '\0';
    n = 0;
    if (overflow) {
      overflow = false;
      continue;
    }
    if (line[0] == '\0') {
      continue;
    }

    unsigned long epoch = 0;
    if (sscanf(line, "TIME %lu", &epoch) == 1) {
      applyTime(epoch);
      continue;
    }
    if (strncmp(line, "ERR ", 4) == 0) {
      unsigned responseId = 0;
      int messageOffset = 0;
      if (sscanf(line, "ERR %8x%n", &responseId, &messageOffset) == 1 &&
          messageOffset == 12 && line[messageOffset] == ' ') {
        if (responseId != requestId) {
          continue;
        }
        ++messageOffset;
      } else {
        messageOffset = 4;
      }
      if (err != nullptr) {
        *err = line + messageOffset;
      }
      return false;
    }
    unsigned len = 0;
    unsigned responseId = 0;
    unsigned crc = 0;
    char extra = '\0';
    if (sscanf(line, "DREAM %8x %u %8x %c", &responseId, &len, &crc,
               &extra) == 3) {
      if (responseId != requestId) {
        portPrintf(port, "ERR %08x stale cook\n", responseId);
        continue;
      }
      if (len < 4 || len > 400000) {
        if (err != nullptr) {
          *err = "dream length";
        }
        return false;
      }
      dreamLen = len;
      dreamCrc = crc;
      return true;
    }
    if (strncmp(line, "DREAM ", 6) == 0) {
      if (err != nullptr) {
        *err = "bridge protocol mismatch";
      }
      return false;
    }
    Console::printf("HOST skip hdr: %.40s\n", line);
  }

  if (err != nullptr) {
    *err = "bridge timeout";
  }
  return false;
}

void pollLinkIdle() {
  char line[96];
  size_t n = 0;
  bool overflow = false;
  for (;;) {
    const int raw = linkOps().idleRead();
    if (raw < 0) {
      break;
    }
    if (raw == '\r') {
      continue;
    }
    if (raw != '\n') {
      if (n + 1 < sizeof(line)) {
        line[n++] = static_cast<char>(raw);
      } else {
        overflow = true;
      }
      continue;
    }
    line[n] = '\0';
    n = 0;
    if (overflow) {
      overflow = false;
      continue;
    }
    noteTime(line);
  }
}

}  // namespace

void linkBind(const LinkOps &ops) { linkOps() = ops; }
void httpBind(const HttpOps &ops) { httpOps() = ops; }
bool httpCookConfigured() { return httpOps().configured(); }
void linkBegin() { linkOps().begin(); }
void linkPoll() { linkOps().poll(); }
void linkSetBusy(bool busy) { linkOps().setBusy(busy); }
bool linkClaim(CookPort *port) { return linkOps().claim(port); }
int linkIdleRead() { return linkOps().idleRead(); }
void linkDrop() { linkOps().drop(); }

void Host::setPump(PumpFn fn) { gPump = fn; }

void Host::poll() {
  linkPoll();
  if (gCooking) {
    return;
  }
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r') {
      continue;
    }
    if (c != '\n') {
      if (gPollLen < sizeof(gPollLine) - 1) {
        gPollLine[gPollLen++] = c;
      }
      continue;
    }
    gPollLine[gPollLen] = '\0';
    gPollLen = 0;
    if (gPollLine[0] != '\0' && isKnownCommand(gPollLine)) {
      handleLine(gPollLine);
    }
  }
  pollLinkIdle();
}

bool Host::cook(const uint8_t *jpeg, size_t jpegLen, const char *styleId,
                const char *takenAt, float lat, float lon, JpegBuffer &outDream,
                String *err) {
  outDream = JpegBuffer{};
  if (jpeg == nullptr || jpegLen == 0) {
    if (err != nullptr) {
      *err = "empty";
    }
    return false;
  }

  gCooking = true;
  struct BusyGuard {
    BusyGuard() { linkSetBusy(true); }
    ~BusyGuard() { linkSetBusy(false); }
  } busyGuard;
  (void)busyGuard;

  if (httpCookConfigured()) {
    resetPollAssembler();
    const bool ok = httpOps().cook(jpeg, jpegLen, styleId, takenAt, lat, lon, outDream,
                                   err, pump);
    resetPollAssembler();
    gCooking = false;
    return ok;
  }

  resetPollAssembler();
  CookPort port{};
  const bool wifi = linkClaim(&port);
  if (!wifi) {
    port = serialPort();
  }
  // Finish a clock sync that was already sitting on this pipe, then start clean.
  drainRx(port);

  const unsigned requestId = esp_random();
  const unsigned jpegCrc = esp_rom_crc32_le(0, jpeg, jpegLen);
  Console::printf("HOST cook %s\n", wifi ? "wifi" : "usb");
  bool framed = portPrintf(port, "COOK style=%s len=%u id=%08x crc=%08x",
                           styleId != nullptr ? styleId : "cartoon",
                           static_cast<unsigned>(jpegLen), requestId, jpegCrc);
  if (framed && takenAt != nullptr && takenAt[0] != '\0') {
    framed = portPrintf(port, " taken_at=%s", takenAt);
  }
  if (framed && !isnan(lat) && !isnan(lon)) {
    framed = portPrintf(port, " lat=%.5f lon=%.5f", lat, lon);
  }
  if (framed) {
    framed = portPrint(port, "\n");
  }
  if (framed) {
    framed = portWriteAll(port, jpeg, jpegLen);
  }
  if (framed) {
    framed = portPrint(port, "\nCOOK_END\n");
  }
  port.flush(port.ctx);

  uint8_t *buf = nullptr;
  auto fail = [&](const char *message) {
    free(buf);
    if (err != nullptr) {
      *err = message;
    }
    portPrintf(port, "ERR %08x %s\n", requestId, message);
    port.flush(port.ctx);
    drainRx(port);
    linkDrop();
    resetPollAssembler();
    gCooking = false;
    return false;
  };

  if (!framed) {
    return fail("link write");
  }

  unsigned dreamLen = 0;
  unsigned dreamCrc = 0;
  String headerErr;
  if (!waitDreamHeader(port, requestId, dreamLen, dreamCrc, &headerErr)) {
    return fail(headerErr.c_str());
  }

  buf = static_cast<uint8_t *>(ps_malloc(dreamLen));
  if (buf == nullptr) {
    buf = static_cast<uint8_t *>(malloc(dreamLen));
  }
  if (buf == nullptr) {
    return fail("oom dream");
  }

  if (!portPrintf(port, "READY DREAM %08x\n", requestId)) {
    return fail("link write");
  }
  port.flush(port.ctx);
  if (!readExact(port, buf, dreamLen, 60000)) {
    return fail("dream short");
  }

  const char expectedTrailer[] = "\nDREAM_END\n";
  uint8_t trailer[sizeof(expectedTrailer) - 1];
  if (!readExact(port, trailer, sizeof(trailer), 5000) ||
      memcmp(trailer, expectedTrailer, sizeof(trailer)) != 0) {
    return fail("dream trailer");
  }
  if (esp_rom_crc32_le(0, buf, dreamLen) != dreamCrc) {
    return fail("dream crc mismatch");
  }
  if (!Image::looksLikeJpeg(buf, dreamLen)) {
    return fail("bad jpeg");
  }

  outDream.data = buf;
  outDream.length = dreamLen;
  outDream.crc32 = dreamCrc;
  resetPollAssembler();
  gCooking = false;
  portPrintf(port, "ACK DREAM %08x\n", requestId);
  port.flush(port.ctx);
  Console::printf("HOST DREAM %u B crc=%08x\n", dreamLen, dreamCrc);
  return true;
}

}  // namespace Somnium
