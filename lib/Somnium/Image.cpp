#include "Image.h"

#include <esp_heap_caps.h>
#include <stdlib.h>
#include <string.h>
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

bool Image::drawJpeg(const uint8_t *data, size_t length, Adafruit_GFX *target,
                     uint8_t jpgScale) {
  if (!looksLikeJpeg(data, length)) {
    return false;
  }
  if (jpgScale != 1 && jpgScale != 2 && jpgScale != 4 && jpgScale != 8) {
    jpgScale = 1;
  }
  if (target == nullptr && !Display::ready()) {
    return false;
  }
  gJpegTarget = target;
  TJpgDec.setJpgScale(jpgScale);
  TJpgDec.setSwapBytes(false);
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

  uint16_t jpgW = 0;
  uint16_t jpgH = 0;
  if (TJpgDec.getJpgSize(&jpgW, &jpgH, data, length) != JDR_OK ||
      jpgW == 0 || jpgH == 0 || jpgW > INT16_MAX || jpgH > INT16_MAX) {
    return false;
  }

  uint8_t scale = 1;
  while (scale < 8) {
    const uint32_t pixels =
        (static_cast<uint32_t>(jpgW) / scale) * (static_cast<uint32_t>(jpgH) / scale);
    if (pixels * 2u <= 160000u) {
      break;
    }
    scale = static_cast<uint8_t>(scale * 2);
  }
  const int16_t srcW = static_cast<int16_t>(jpgW / scale);
  const int16_t srcH = static_cast<int16_t>(jpgH / scale);
  const size_t nativeBytes =
      static_cast<size_t>(srcW) * static_cast<size_t>(srcH) * 2;
  auto *native = static_cast<uint8_t *>(
      heap_caps_malloc(nativeBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (native == nullptr) {
    native = static_cast<uint8_t *>(malloc(nativeBytes));
  }
  if (native == nullptr) {
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

   private:
    uint16_t *px_;
    int16_t w_;
    int16_t h_;
  };
  BufGfx gfx(srcW, srcH, reinterpret_cast<uint16_t *>(native));
  const bool decoded = drawJpeg(data, length, &gfx, scale);
  if (!decoded) {
    free(native);
    return false;
  }

  auto *src = reinterpret_cast<const uint16_t *>(native);
  if (srcW == outW && srcH == outH) {
    memcpy(out, src, nativeBytes);
    free(native);
    return true;
  }

  // Letterbox onto the 240x135 panel (or a smaller BLE preview).
  const float scaleX = static_cast<float>(outW) / static_cast<float>(srcW);
  const float scaleY = static_cast<float>(outH) / static_cast<float>(srcH);
  const float s = scaleX < scaleY ? scaleX : scaleY;
  const int16_t dw = static_cast<int16_t>(srcW * s + 0.5f);
  const int16_t dh = static_cast<int16_t>(srcH * s + 0.5f);
  const int16_t ox = static_cast<int16_t>((outW - dw) / 2);
  const int16_t oy = static_cast<int16_t>((outH - dh) / 2);
  const size_t outN = static_cast<size_t>(outW) * static_cast<size_t>(outH);
  for (size_t i = 0; i < outN; ++i) {
    out[i] = 0;
  }
  for (int16_t y = 0; y < dh; ++y) {
    const int16_t sy =
        static_cast<int16_t>((static_cast<int32_t>(y) * srcH) / dh);
    for (int16_t x = 0; x < dw; ++x) {
      const int16_t sx =
          static_cast<int16_t>((static_cast<int32_t>(x) * srcW) / dw);
      out[static_cast<int32_t>(oy + y) * outW + (ox + x)] =
          src[static_cast<int32_t>(sy) * srcW + sx];
    }
    if ((y & 15) == 0) {
      yield();
    }
  }
  free(native);
  return true;
}

}  // namespace Somnium
