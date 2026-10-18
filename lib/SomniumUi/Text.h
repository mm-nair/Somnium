#pragma once

#include "Canvas.h"
#include "Types.h"

namespace Somnium {
namespace Ui {

struct TextStyle {
  Color color;
  uint8_t size;
  Align align;
  VAlign valign;

  TextStyle()
      : color(Color::rgb565(255, 255, 255)),
        size(1),
        align(Align::Left),
        valign(VAlign::Top) {}
  TextStyle(Color c, uint8_t s, Align a = Align::Left,
            VAlign v = VAlign::Top)
      : color(c), size(s), align(a), valign(v) {}
};

struct Text {
  static constexpr int16_t glyphW(uint8_t size) {
    return static_cast<int16_t>(6 * (size == 0 ? 1 : size));
  }
  static constexpr int16_t glyphH(uint8_t size) {
    return static_cast<int16_t>(8 * (size == 0 ? 1 : size));
  }

  static Size measure(const char *text, uint8_t size);
  static Size measureWrapped(const char *text, uint8_t size, int16_t maxWidth);

  static void draw(Canvas &canvas, const char *text, int16_t x, int16_t y,
                   const TextStyle &style);
  static void drawIn(Canvas &canvas, const char *text, Rect box,
                     const TextStyle &style);
  static void drawFontIn(Canvas &canvas, const char *text, Rect box,
                         const GFXfont &font, Color color);
  static void drawWrapped(Canvas &canvas, const char *text, Rect box,
                          const TextStyle &style);
};

}  // namespace Ui
}  // namespace Somnium
