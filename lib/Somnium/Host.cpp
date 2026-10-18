#include "Host.h"

#include <Arduino.h>
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

void pump() {
  if (gPump != nullptr) {
    gPump();
  } else {
    yield();
  }
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
  // Stay quiet on the wire during COOK↔DREAM — TX log lines can race RX.
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
    if (!Camera::ready()) {
      Console::println("ERR camera");
      return;
    }
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
    if (!Camera::ready()) {
      Console::println("ERR camera");
      return;
    }
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

  Console::println("ERR cmd");
}

bool readExact(uint8_t *dst, size_t len, uint32_t timeoutMs) {
  // No UI pump here — Process SPI blits starve UART RX and drop DREAM bytes.
  size_t got = 0;
  const unsigned long deadline = millis() + timeoutMs;
  while (got < len && millis() < deadline) {
    if (Serial.available() <= 0) {
      yield();
      delay(1);
      continue;
    }
    got += Serial.readBytes(dst + got, len - got);
  }
  return got == len;
}

bool readLine(char *buf, size_t buflen, uint32_t timeoutMs, bool allowPump) {
  size_t n = 0;
  const unsigned long deadline = millis() + timeoutMs;
  while (millis() < deadline && n + 1 < buflen) {
    if (allowPump) {
      pump();
    } else {
      yield();
    }
    if (Serial.available() <= 0) {
      delay(1);
      continue;
    }
    const char c = static_cast<char>(Serial.read());
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      buf[n] = '\0';
      return true;
    }
    buf[n++] = c;
  }
  buf[n] = '\0';
  return false;
}

}  // namespace

void Host::setPump(PumpFn fn) { gPump = fn; }

void Host::poll() {
  if (gCooking) {
    return;
  }
  static char line[96];
  static size_t length = 0;
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r') {
      continue;
    }
    if (c != '\n') {
      if (length < sizeof(line) - 1) {
        line[length++] = c;
      }
      continue;
    }
    line[length] = '\0';
    length = 0;
    if (line[0] != '\0') {
      handleLine(line);
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
  while (Serial.available() > 0) {
    Serial.read();
  }

  Serial.printf("COOK style=%s len=%u", styleId != nullptr ? styleId : "cartoon",
                static_cast<unsigned>(jpegLen));
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

  char line[96];
  const unsigned long deadline = millis() + 180000;
  while (true) {
    if (millis() >= deadline) {
      gCooking = false;
      if (err != nullptr) {
        *err = "bridge timeout";
      }
      return false;
    }
    // Pump UI while waiting on xAI; stop once payload starts arriving.
    if (!readLine(line, sizeof(line), 5000, /*allowPump=*/true)) {
      continue;
    }
    unsigned long epoch = 0;
    if (sscanf(line, "TIME %lu", &epoch) == 1) {
      applyTime(epoch);
      continue;
    }
    break;
  }

  if (strncmp(line, "ERR ", 4) == 0) {
    gCooking = false;
    if (err != nullptr) {
      *err = line + 4;
    }
    return false;
  }

  unsigned dreamLen = 0;
  if (sscanf(line, "DREAM %u", &dreamLen) != 1 || dreamLen < 4 ||
      dreamLen > 400000) {
    gCooking = false;
    if (err != nullptr) {
      *err = "bad dream hdr";
    }
    Console::printf("HOST bad dream hdr: %s\n", line);
    return false;
  }

  uint8_t *buf = static_cast<uint8_t *>(ps_malloc(dreamLen));
  if (buf == nullptr) {
    buf = static_cast<uint8_t *>(malloc(dreamLen));
  }
  if (buf == nullptr) {
    gCooking = false;
    if (err != nullptr) {
      *err = "oom dream";
    }
    return false;
  }
  if (!readExact(buf, dreamLen, 60000)) {
    free(buf);
    gCooking = false;
    if (err != nullptr) {
      *err = "dream short";
    }
    return false;
  }
  char end[16];
  readLine(end, sizeof(end), 5000, /*allowPump=*/false);

  if (buf[0] != 0xFF || buf[1] != 0xD8) {
    free(buf);
    gCooking = false;
    if (err != nullptr) {
      *err = "bad jpeg";
    }
    return false;
  }

  outDream.data = buf;
  outDream.length = dreamLen;
  gCooking = false;
  Console::printf("HOST DREAM %u B\n", dreamLen);
  return true;
}

}  // namespace Somnium
