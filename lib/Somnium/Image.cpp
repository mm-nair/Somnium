#include "Image.h"

#include "Config.h"
#include "Console.h"

namespace Somnium {

bool Image::takePhoto(JpegBuffer &out, int width, int jpegQuality) {
  if (!Camera::captureJpeg(out, width, jpegQuality)) {
    Console::println("Image: capture failed");
    return false;
  }
  Console::printf("Image: JPEG %u bytes crc=%08x capture=%lu ms encode=%lu ms "
                  "(%dx%d)\n",
                  static_cast<unsigned>(out.length), out.crc32, out.captureMs,
                  out.encodeMs, out.width, out.height);
  return true;
}

void Image::freePhoto(JpegBuffer &buffer) { Camera::release(buffer); }

bool Image::looksLikeJpeg(const uint8_t *data, size_t length) {
  return data != nullptr && length >= 4 && data[0] == 0xFF && data[1] == 0xD8 &&
         data[length - 2] == 0xFF && data[length - 1] == 0xD9;
}

bool Image::drawJpeg(const uint8_t *data, size_t length) {
  (void)data;
  (void)length;
  // Intentionally unimplemented: wire TJpg_Decoder / JPEGDEC here when the
  // backend reply path lands. Local shots use Display::drawCameraFrame via RGB565.
  Console::println("Image: drawJpeg not implemented yet");
  return false;
}

}  // namespace Somnium
