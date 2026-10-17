#include "Canvas.h"

#include <Adafruit_SPITFT.h>

namespace Somnium {
namespace Ui {

void Canvas::bind(Adafruit_GFX *gfx, int16_t width, int16_t height) {
  gfx_ = gfx;
  width_ = width;
  height_ = height;
  resetOrigin();
  resetClip();
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

void Canvas::resetClip() { clip_ = {0, 0, width_, height_}; }

Rect Canvas::mapRect(Rect r) const {
  return {static_cast<int16_t>(r.x + originX_),
          static_cast<int16_t>(r.y + originY_), r.w, r.h};
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

void Canvas::clear(Color color) { fillRect(bounds(), color); }

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
  fillRect({x, y, w, 1}, color);
}

void Canvas::vLine(int16_t x, int16_t y, int16_t h, Color color) {
  fillRect({x, y, 1, h}, color);
}

void Canvas::blitRgb565(int16_t x, int16_t y, int16_t w, int16_t h,
                        const uint16_t *pixels) {
  if (gfx_ == nullptr || pixels == nullptr || w <= 0 || h <= 0) {
    return;
  }
  auto *tft = static_cast<Adafruit_SPITFT *>(gfx_);
  Rect r = mapRect({x, y, w, h});
  const Rect visible = Rect::intersect(r, clip_);
  if (visible.empty()) {
    return;
  }
  const int16_t srcX = static_cast<int16_t>(visible.x - r.x);
  const int16_t srcY = static_cast<int16_t>(visible.y - r.y);
  tft->startWrite();
  tft->setAddrWindow(visible.x, visible.y, visible.w, visible.h);
  for (int16_t row = 0; row < visible.h; ++row) {
    const uint16_t *line = pixels + (srcY + row) * w + srcX;
    tft->writePixels(const_cast<uint16_t *>(line), visible.w);
  }
  tft->endWrite();
}

}  // namespace Ui
}  // namespace Somnium
