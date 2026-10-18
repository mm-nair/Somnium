#pragma once

#include <Adafruit_GFX.h>
#include <Arduino.h>

#include "Camera.h"
#include "Config.h"

namespace Somnium {

class Image {
 public:
  static bool takePhoto(JpegBuffer &out, int width = Defaults::kCameraWidth,
                        int jpegQuality = Defaults::kJpegQuality);
  static void freePhoto(JpegBuffer &buffer);
  static bool looksLikeJpeg(const uint8_t *data, size_t length);
  // Draw onto target (backbuffer) or the LCD if target is null.
  static bool drawJpeg(const uint8_t *data, size_t length,
                       Adafruit_GFX *target = nullptr);
};

}  // namespace Somnium
