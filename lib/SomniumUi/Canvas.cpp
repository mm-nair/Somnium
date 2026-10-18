#include "Canvas.h"

#include <esp_heap_caps.h>

namespace Somnium {
namespace Ui {
namespace {

class PixelCanvas : public Adafruit_GFX {
 public:
  PixelCanvas(int16_t w, int16_t h, uint16_t *pixels)
      : Adafruit_GFX(w, h), pixels_(pixels) {}

  void drawPixel(int16_t x, int16_t y, uint16_t color) override {
    if (pixels_ == nullptr || x < 0 || y < 0 || x >= _width || y >= _height) {
      return;
    }
    pixels_[static_cast<int32_t>(y) * _width + x] = color;
  }

  void fillScreen(uint16_t color) override {
    if (pixels_ == nullptr) {
      return;
    }
    const size_t n = static_cast<size_t>(_width) * static_cast<size_t>(_height);
    for (size_t i = 0; i < n; ++i) {
      pixels_[i] = color;
    }
  }

  uint16_t *pixels() const { return pixels_; }

 private:
  uint16_t *pixels_;
};

}  // namespace

void Canvas::bind(Adafruit_SPITFT *tft, int16_t width, int16_t height) {
  tft_ = tft;
  width_ = width;
  height_ = height;
  const size_t bytes =
      static_cast<size_t>(width) * static_cast<size_t>(height) * sizeof(uint16_t);
  fbPixels_ = static_cast<uint16_t *>(
      heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (fbPixels_ == nullptr) {
    fbPixels_ = static_cast<uint16_t *>(malloc(bytes));
  }
  if (fbPixels_ != nullptr) {
    fb_ = new PixelCanvas(width, height, fbPixels_);
    gfx_ = fb_;
  } else {
    fb_ = nullptr;
    gfx_ = tft;
  }
  resetOrigin();
  resetClip();
  frameOpen_ = false;
}

void Canvas::beginFrame() {
  gfx_ = fb_ != nullptr ? fb_ : static_cast<Adafruit_GFX *>(tft_);
  resetOrigin();
  resetClip();
  frameOpen_ = true;
}

void Canvas::endFrame() {
  if (!frameOpen_) {
    return;
  }
  frameOpen_ = false;
  if (fbPixels_ != nullptr && tft_ != nullptr) {
    tft_->drawRGBBitmap(0, 0, fbPixels_, width_, height_);
  }
  gfx_ = tft_;
}

void Canvas::setOrigin(int16_t x, int16_t y) {
  originX_ = x;
  originY_ = y;
}

void Canvas::resetOrigin() {
  originX_ = 0;
  originY_ = 0;
}

void Canvas::setClip(Rect clip) { clip_ = clip; }

void Canvas::resetClip() { clip_ = Rect(0, 0, width_, height_); }

Rect Canvas::mapRect(Rect r) const {
  return Rect(static_cast<int16_t>(r.x + originX_),
              static_cast<int16_t>(r.y + originY_), r.w, r.h);
}

bool Canvas::clipRect(Rect &r) const {
  r = Rect::intersect(r, clip_);
  return !r.empty();
}

void Canvas::fillScreen(Color color) {
  if (gfx_ != nullptr) {
    gfx_->fillScreen(color);
  }
}

void Canvas::clear(Color color) { fillScreen(color); }

void Canvas::fillRect(Rect r, Color color) {
  if (gfx_ == nullptr) {
    return;
  }
  r = mapRect(r);
  if (!clipRect(r)) {
    return;
  }
  gfx_->fillRect(r.x, r.y, r.w, r.h, color);
}

void Canvas::drawRect(Rect r, Color color) {
  if (gfx_ == nullptr) {
    return;
  }
  r = mapRect(r);
  if (!clipRect(r)) {
    return;
  }
  gfx_->drawRect(r.x, r.y, r.w, r.h, color);
}

void Canvas::hLine(int16_t x, int16_t y, int16_t w, Color color) {
  fillRect(Rect(x, y, w, 1), color);
}

void Canvas::vLine(int16_t x, int16_t y, int16_t h, Color color) {
  fillRect(Rect(x, y, 1, h), color);
}

void Canvas::blitRgb565(int16_t x, int16_t y, int16_t w, int16_t h,
                        const uint16_t *pixels) {
  if (gfx_ == nullptr || pixels == nullptr || w <= 0 || h <= 0) {
    return;
  }
  Rect r = mapRect(Rect(x, y, w, h));
  const Rect visible = Rect::intersect(r, clip_);
  if (visible.empty()) {
    return;
  }
  const int16_t srcX = static_cast<int16_t>(visible.x - r.x);
  const int16_t srcY = static_cast<int16_t>(visible.y - r.y);
  for (int16_t row = 0; row < visible.h; ++row) {
    const uint16_t *line = pixels + (srcY + row) * w + srcX;
    for (int16_t col = 0; col < visible.w; ++col) {
      gfx_->drawPixel(static_cast<int16_t>(visible.x + col),
                      static_cast<int16_t>(visible.y + row), line[col]);
    }
  }
}

}  // namespace Ui
}  // namespace Somnium
