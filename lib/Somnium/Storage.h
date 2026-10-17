#pragma once

#include <Arduino.h>

namespace Somnium {

class Storage {
 public:
  static bool begin();
  static bool ready();
  // Writes JPEG bytes; name like "/somnium/20260101T120000Z.jpg"
  static bool saveJpeg(const char *path, const uint8_t *data, size_t length);
};

}  // namespace Somnium
