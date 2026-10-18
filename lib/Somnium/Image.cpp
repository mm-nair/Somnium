#include "Image.h"

#include <TJpg_Decoder.h>

#include "Display.h"

namespace Somnium {
namespace {

Adafruit_GFX *gJpegTarget = nullptr;

bool jpegDrawToken(int16_t x, int16_t y, uint16_t w, uint16_t h,
                   uint16_t *bitmap) {
  Adafruit_GFX *gfx = gJpegTarget;
  if (gfx == nullptr) {
    if (!Display::ready() || Display::tft() == nullptr) {
      return false;
    }
    gfx = Display::tft();
  }
  gfx->drawRGBBitmap(x, y, bitmap, w, h);
  return true;
}

bool targetIsTft(Adafruit_GFX *target) {
  if (target == nullptr) {
    return true;
  }
  return Display::ready() && target == static_cast<Adafruit_GFX *>(Display::tft());
}

}  // namespace

bool Image::takePhoto(JpegBuffer &out, int width, int jpegQuality) {
  return Camera::captureJpeg(out, width, jpegQuality);
}

void Image::freePhoto(JpegBuffer &buffer) { Camera::release(buffer); }

bool Image::looksLikeJpeg(const uint8_t *data, size_t length) {
  if (data == nullptr || length < 4 || data[0] != 0xFF || data[1] != 0xD8) {
    return false;
  }
  // Accept EOI at end, or within a short trailer (HTTP padding / writers).
  const size_t scanFrom = length > 32 ? length - 32 : 2;
  for (size_t i = length - 1; i >= scanFrom; --i) {
    if (data[i - 1] == 0xFF && data[i] == 0xD9) {
      return true;
    }
  }
  return false;
}

bool Image::drawJpeg(const uint8_t *data, size_t length, Adafruit_GFX *target) {
  if (!looksLikeJpeg(data, length)) {
    return false;
  }
  if (target == nullptr && !Display::ready()) {
    return false;
  }
  gJpegTarget = target;
  TJpgDec.setJpgScale(1);
  // ILI9341 SPI wants swapped RGB565; RAM framebuffer (PixelCanvas) wants
  // native uint16_t order. Swapping into the FB → rainbow / noise on blit.
  TJpgDec.setSwapBytes(targetIsTft(target));
  TJpgDec.setCallback(jpegDrawToken);
  const bool ok = TJpgDec.drawJpg(0, 0, data, length) == JDR_OK;
  gJpegTarget = nullptr;
  return ok;
}

bool Image::decodeRgb565(const uint8_t *data, size_t length, uint16_t *out,
                         int16_t outW, int16_t outH) {
  if (out == nullptr || outW <= 0 || outH <= 0 ||
      !looksLikeJpeg(data, length)) {
    return false;
  }
  class BufGfx : public Adafruit_GFX {
   public:
    BufGfx(int16_t w, int16_t h, uint16_t *px)
        : Adafruit_GFX(w, h), px_(px), w_(w), h_(h) {}
    void drawPixel(int16_t x, int16_t y, uint16_t color) override {
      if (px_ == nullptr || x < 0 || y < 0 || x >= w_ || y >= h_) {
        return;
      }
      px_[static_cast<int32_t>(y) * w_ + x] = color;
    }
    void fillScreen(uint16_t color) override {
      const size_t n = static_cast<size_t>(w_) * static_cast<size_t>(h_);
      for (size_t i = 0; i < n; ++i) {
        px_[i] = color;
      }
    }

   private:
    uint16_t *px_;
    int16_t w_;
    int16_t h_;
  } gfx(outW, outH, out);
  gfx.fillScreen(0);
  return drawJpeg(data, length, &gfx);
}

}  // namespace Somnium
