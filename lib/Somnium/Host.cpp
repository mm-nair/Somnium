#include "Host.h"

#include <Arduino.h>

#include "Camera.h"
#include "Config.h"
#include "Console.h"
#include "Image.h"

namespace Somnium {
namespace {

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
    Serial.printf("OK somnium %lu\n", static_cast<unsigned long>(Defaults::kSerialBaud));
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

  Console::println("ERR cmd");
}

}  // namespace

void Host::poll() {
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

}  // namespace Somnium
