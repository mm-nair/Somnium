#include "Host.h"

#include <Arduino.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "Camera.h"
#include "Config.h"
#include "Console.h"
#include "Image.h"

namespace Somnium {
namespace {

bool gCooking = false;

void sendPhoto(JpegBuffer &photo) {
  Serial.printf("PHOTO %u %08x %lu %lu %d %d\n",
                static_cast<unsigned>(photo.length), photo.crc32, photo.captureMs,
                photo.encodeMs, photo.width, photo.height);
  Serial.write(photo.data, photo.length);
  Serial.print("\nPHOTO_END\n");
  Serial.flush();
}

void handleLine(char *line) {
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

  Console::println("ERR cmd");
}

bool readExact(uint8_t *dst, size_t len, uint32_t timeoutMs) {
  size_t got = 0;
  const unsigned long deadline = millis() + timeoutMs;
  while (got < len && millis() < deadline) {
    if (Serial.available() <= 0) {
      delay(1);
      yield();
      continue;
    }
    got += Serial.readBytes(dst + got, len - got);
  }
  return got == len;
}

bool readLine(char *buf, size_t buflen, uint32_t timeoutMs) {
  size_t n = 0;
  const unsigned long deadline = millis() + timeoutMs;
  while (millis() < deadline && n + 1 < buflen) {
    if (Serial.available() <= 0) {
      delay(1);
      yield();
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
  // Drain inbound junk so we don't confuse the bridge.
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
  Console::printf("HOST COOK %u B → bridge\n", static_cast<unsigned>(jpegLen));

  char line[96];
  if (!readLine(line, sizeof(line), 180000)) {
    gCooking = false;
    if (err != nullptr) {
      *err = "bridge timeout";
    }
    return false;
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
  readLine(end, sizeof(end), 5000);  // optional trailing DREAM_END

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
