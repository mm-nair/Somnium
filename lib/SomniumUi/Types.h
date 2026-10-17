#pragma once

#include <Arduino.h>

namespace Somnium {
namespace Ui {

struct Point {
  int16_t x;
  int16_t y;

  Point() : x(0), y(0) {}
  Point(int16_t xVal, int16_t yVal) : x(xVal), y(yVal) {}
};

struct Size {
  int16_t w;
  int16_t h;

  Size() : w(0), h(0) {}
  Size(int16_t wVal, int16_t hVal) : w(wVal), h(hVal) {}
};

struct Rect {
  int16_t x;
  int16_t y;
  int16_t w;
  int16_t h;

  Rect() : x(0), y(0), w(0), h(0) {}
  Rect(int16_t xVal, int16_t yVal, int16_t wVal, int16_t hVal)
      : x(xVal), y(yVal), w(wVal), h(hVal) {}

  int16_t right() const { return static_cast<int16_t>(x + w); }
  int16_t bottom() const { return static_cast<int16_t>(y + h); }
  bool empty() const { return w <= 0 || h <= 0; }

  bool contains(Point p) const {
    return p.x >= x && p.y >= y && p.x < right() && p.y < bottom();
  }

  Rect inset(int16_t all) const {
    return Rect(static_cast<int16_t>(x + all), static_cast<int16_t>(y + all),
                static_cast<int16_t>(w - all * 2),
                static_cast<int16_t>(h - all * 2));
  }

  static Rect intersect(Rect a, Rect b) {
    const int16_t x1 = a.x > b.x ? a.x : b.x;
    const int16_t y1 = a.y > b.y ? a.y : b.y;
    const int16_t x2 = a.right() < b.right() ? a.right() : b.right();
    const int16_t y2 = a.bottom() < b.bottom() ? a.bottom() : b.bottom();
    if (x2 <= x1 || y2 <= y1) {
      return Rect();
    }
    return Rect(x1, y1, static_cast<int16_t>(x2 - x1),
                static_cast<int16_t>(y2 - y1));
  }
};

enum class Align : uint8_t { Left, Center, Right };
enum class VAlign : uint8_t { Top, Middle, Bottom };

struct Color {
  uint16_t value;

  Color() : value(0) {}
  explicit Color(uint16_t v) : value(v) {}
  operator uint16_t() const { return value; }

  static Color rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return Color(static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) |
                                       (b >> 3)));
  }
};

}  // namespace Ui
}  // namespace Somnium
