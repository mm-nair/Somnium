#include "Image.h"

#include <TJpg_Decoder.h>

#include "Display.h"

namespace Somnium {
namespace {

bool jpegDrawToken(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap) {
  if (!Display::ready() || Display::tft() == nullptr) {
    return false;
  }
  Display::tft()->drawRGBBitmap(x, y, bitmap, w, h);
  return true;
}

}  // namespace

bool Image::takePhoto(JpegBuffer &out, int width, int jpegQuality) {
  return Camera::captureJpeg(out, width, jpegQuality);
}

void Image::freePhoto(JpegBuffer &buffer) { Camera::release(buffer); }

bool Image::looksLikeJpeg(const uint8_t *data, size_t length) {
  return data != nullptr && length >= 4 && data[0] == 0xFF && data[1] == 0xD8 &&
         data[length - 2] == 0xFF && data[length - 1] == 0xD9;
}

bool Image::drawJpeg(const uint8_t *data, size_t length) {
  if (!looksLikeJpeg(data, length) || !Display::ready()) {
    return false;
  }
  TJpgDec.setJpgScale(1);
  TJpgDec.setSwapBytes(true);
  TJpgDec.setCallback(jpegDrawToken);
  return TJpgDec.drawJpg(0, 0, data, length) == JDR_OK;
}

}  // namespace Somnium
