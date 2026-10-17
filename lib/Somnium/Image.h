#pragma once

#include <Arduino.h>

#include "Camera.h"
#include "Config.h"

namespace Somnium {

// Image helpers around captured JPEGs. Decode-to-LCD for API replies can plug
// in here later (TJpg_Decoder / JPEGDEC) without touching Camera/Display.
class Image {
 public:
  // Take a still; Camera frame callback draws RGB565 to the LCD when Display
  // has been begun. Returns false on failure.
  static bool takePhoto(JpegBuffer &out,
                        int width = Defaults::kCameraWidth,
                        int jpegQuality = Defaults::kJpegQuality);

  static void freePhoto(JpegBuffer &buffer);

  // Validate SOI/EOI markers on a JPEG buffer (host or future HTTP reply).
  static bool looksLikeJpeg(const uint8_t *data, size_t length);

  // Placeholder for rendering a JPEG buffer on the LCD (e.g. stylized reply).
  // Returns false until a decoder is wired in.
  static bool drawJpeg(const uint8_t *data, size_t length);
};

}  // namespace Somnium
