#pragma once

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
};

}  // namespace Somnium
