#include "Image.h"

namespace Somnium {

bool Image::takePhoto(JpegBuffer &out, int width, int jpegQuality) {
  return Camera::captureJpeg(out, width, jpegQuality);
}

void Image::freePhoto(JpegBuffer &buffer) { Camera::release(buffer); }

bool Image::looksLikeJpeg(const uint8_t *data, size_t length) {
  return data != nullptr && length >= 4 && data[0] == 0xFF && data[1] == 0xD8 &&
         data[length - 2] == 0xFF && data[length - 1] == 0xD9;
}

}  // namespace Somnium
