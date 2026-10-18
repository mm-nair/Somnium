#pragma once

#include <Arduino.h>

namespace Somnium {

class Storage {
 public:
  static bool begin();
  static bool ready();
  // Writes JPEG bytes (truncates/replaces existing file).
  static bool saveJpeg(const char *path, const uint8_t *data, size_t length);

  // Wipe /somnium/*.jpg (called on boot so stale/corrupt files can't poison browse).
  static void clearGallery();

  // Gallery helpers (scans /somnium for *.jpg). Index is 0-based.
  static int photoCount();
  static bool photoPathAt(int index, char *out, size_t outLen);
  static bool loadJpeg(const char *path, uint8_t **outData, size_t *outLen);
  static void freeJpeg(uint8_t *data);
};

}  // namespace Somnium
