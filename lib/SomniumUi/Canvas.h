#pragma once

#include <Adafruit_GFX.h>
#include <Adafruit_SPITFT.h>

#include "Theme.h"
#include "Types.h"

namespace Somnium {
namespace Ui {

// Draws into a RAM/PSRAM backbuffer, then blits once to the LCD so the user
// never sees a progressive black fillScreen "wipe" over SPI.
class Canvas {
 public:
  void bind(Adafruit_SPITFT *tft, int16_t width, int16_t height);
  Adafruit_GFX *gfx() const { return gfx_; }
  Adafruit_SPITFT *tft() const { return tft_; }

  int16_t width() const { return width_; }
  int16_t height() const { return height_; }
  Rect bounds() const { return Rect(0, 0, width_, height_); }

  void beginFrame();
  void endFrame();

  void setOrigin(int16_t x, int16_t y);
  Point origin() const { return Point(originX_, originY_); }
  void resetOrigin();

  void setClip(Rect clip);
  void resetClip();
  Rect clip() const { return clip_; }

  void clear(Color color);
  void fillRect(Rect r, Color color);
  void drawRect(Rect r, Color color);
  void hLine(int16_t x, int16_t y, int16_t w, Color color);
  void vLine(int16_t x, int16_t y, int16_t h, Color color);
  void fillScreen(Color color);

  void blitRgb565(int16_t x, int16_t y, int16_t w, int16_t h,
                  const uint16_t *pixels);

 private:
  Rect mapRect(Rect r) const;
  bool clipRect(Rect &r) const;

  Adafruit_SPITFT *tft_ = nullptr;
  Adafruit_GFX *fb_ = nullptr;
  Adafruit_GFX *gfx_ = nullptr;
  uint16_t *fbPixels_ = nullptr;
  int16_t width_ = 0;
  int16_t height_ = 0;
  int16_t originX_ = 0;
  int16_t originY_ = 0;
  Rect clip_{};
  bool frameOpen_ = false;
};

}  // namespace Ui
}  // namespace Somnium
