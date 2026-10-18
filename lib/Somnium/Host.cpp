#include "Host.h"

#include <Arduino.h>
#include <esp_rom_crc.h>
#include <esp_system.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "Camera.h"
#include "Config.h"
#include "Console.h"
#include "Image.h"
#include "Storage.h"

namespace Somnium {
namespace {

bool gCooking = false;
Host::PumpFn gPump = nullptr;

char gPollLine[96];
size_t gPollLen = 0;

void pump() {
  if (gPump != nullptr) {
    gPump();
  } else {
    yield();
  }
}

void drainRx() {
  while (Serial.available() > 0) {
    Serial.read();
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

bool readExact(uint8_t *dst, size_t len, uint32_t timeoutMs) {
  size_t got = 0;
  const unsigned long started = millis();
  while (got < len && millis() - started < timeoutMs) {
    if (Serial.available() <= 0) {
      yield();
      delay(1);
      continue;
    }
    const size_t available = static_cast<size_t>(Serial.available());
    const size_t remaining = len - got;
    got += Serial.readBytes(dst + got,
                            available < remaining ? available : remaining);
  }
  return got == len;
}

bool waitDreamHeader(unsigned requestId, unsigned &dreamLen,
                     unsigned &dreamCrc, String *err) {
  char line[96];
  size_t n = 0;
  bool overflow = false;
  const unsigned long started = millis();
  unsigned long lastPump = started;

  while (millis() - started < Defaults::kCookTimeoutMs) {
    if (Serial.available() <= 0) {
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

    const char c = static_cast<char>(Serial.read());
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
        Serial.printf("ERR %08x stale cook\n", responseId);
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

}  // namespace

void Host::setPump(PumpFn fn) { gPump = fn; }

void Host::poll() {
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
  resetPollAssembler();
  drainRx();

  const unsigned requestId = esp_random();
  const unsigned jpegCrc = esp_rom_crc32_le(0, jpeg, jpegLen);
  Serial.printf("COOK style=%s len=%u id=%08x crc=%08x",
                styleId != nullptr ? styleId : "cartoon",
                static_cast<unsigned>(jpegLen), requestId, jpegCrc);
  if (takenAt != nullptr && takenAt[0] != '\0') {
    Serial.printf(" taken_at=%s", takenAt);
  }
  if (!isnan(lat) && !isnan(lon)) {
    Serial.printf(" lat=%.5f lon=%.5f", lat, lon);
  }
  Serial.print("\n");
  Serial.write(jpeg, jpegLen);
  Serial.print("\nCOOK_END\n");
  Serial.flush();

  uint8_t *buf = nullptr;
  auto fail = [&](const char *message) {
    free(buf);
    if (err != nullptr) {
      *err = message;
    }
    Serial.printf("ERR %08x %s\n", requestId, message);
    Serial.flush();
    drainRx();
    resetPollAssembler();
    gCooking = false;
    return false;
  };

  unsigned dreamLen = 0;
  unsigned dreamCrc = 0;
  String headerErr;
  if (!waitDreamHeader(requestId, dreamLen, dreamCrc, &headerErr)) {
    return fail(headerErr.c_str());
  }

  buf = static_cast<uint8_t *>(ps_malloc(dreamLen));
  if (buf == nullptr) {
    buf = static_cast<uint8_t *>(malloc(dreamLen));
  }
  if (buf == nullptr) {
    return fail("oom dream");
  }

  Serial.printf("READY DREAM %08x\n", requestId);
  Serial.flush();
  if (!readExact(buf, dreamLen, 60000)) {
    return fail("dream short");
  }

  const char expectedTrailer[] = "\nDREAM_END\n";
  uint8_t trailer[sizeof(expectedTrailer) - 1];
  if (!readExact(trailer, sizeof(trailer), 5000) ||
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
  Serial.printf("ACK DREAM %08x\n", requestId);
  Serial.flush();
  Console::printf("HOST DREAM %u B crc=%08x\n", dreamLen, dreamCrc);
  return true;
}

}  // namespace Somnium
