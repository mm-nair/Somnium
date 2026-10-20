from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
GFX = ROOT / ".pio/libdeps/somnium/Adafruit GFX Library"


class UiTextTests(unittest.TestCase):
    def test_text_pixels_fit_their_boxes(self):
        headers = {
            "Arduino.h": r"""
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
using String = std::string;
class __FlashStringHelper;
inline void yield() {}
inline float radians(float degrees) { return degrees * 0.017453292519943295f; }
#define PROGMEM
#define pgm_read_byte(address) (*(const uint8_t *)(address))
#define pgm_read_word(address) (*(const uint16_t *)(address))
""",
            "Print.h": r"""
#pragma once
#include "Arduino.h"
class Print {
 public:
  virtual size_t write(uint8_t character) = 0;
  size_t print(const char *text) {
    size_t length = 0;
    while (*text != '\0') { length += write(*text++); }
    return length;
  }
};
""",
            "Adafruit_I2CDevice.h": "#pragma once\n",
            "Adafruit_SPIDevice.h": "#pragma once\n",
            "Adafruit_SPITFT.h": r"""
#pragma once
#include "Adafruit_GFX.h"
class Adafruit_SPITFT : public GFXcanvas16 {
 public:
  using GFXcanvas16::GFXcanvas16;
};
""",
        }
        harness = r"""
#include <cassert>
#include <algorithm>
#include <vector>
#include "Text.h"
#include "Fonts/FreeSerifBoldItalic12pt7b.h"

namespace Somnium {
namespace Ui {
void Canvas::bind(Adafruit_SPITFT *tft, int16_t width, int16_t height) {
  gfx_ = tft;
  width_ = width;
  height_ = height;
  clip_ = bounds();
}
void Canvas::setOrigin(int16_t x, int16_t y) {
  originX_ = x;
  originY_ = y;
}
void Canvas::setClip(Rect clip) { clip_ = clip; }
void Canvas::resetClip() { clip_ = bounds(); }
void Canvas::fillRect(Rect rectangle, Color color) {
  rectangle.x += originX_;
  rectangle.y += originY_;
  rectangle = Rect::intersect(rectangle, clip_);
  gfx_->fillRect(rectangle.x, rectangle.y, rectangle.w, rectangle.h, color);
}
}
}

using namespace Somnium::Ui;

static int pixelCount(GFXcanvas16 &canvas, Rect box) {
  int count = 0;
  for (int16_t y = 0; y < canvas.height(); ++y) {
    for (int16_t x = 0; x < canvas.width(); ++x) {
      if (canvas.getPixel(x, y) != 0) {
        assert(box.contains({x, y}));
        ++count;
      }
    }
  }
  return count;
}

static void samePixels(GFXcanvas16 &actual, GFXcanvas16 &expected) {
  for (int16_t y = 0; y < actual.height(); ++y) {
    for (int16_t x = 0; x < actual.width(); ++x) {
      assert(actual.getPixel(x, y) == expected.getPixel(x, y));
    }
  }
}

int main() {
  constexpr uint16_t color = 0xFB56;
  Adafruit_SPITFT actual(320, 240);
  GFXcanvas16 expected(320, 240);
  Canvas canvas;
  canvas.bind(&actual, 320, 240);
  const GFXfont &font = FreeSerifBoldItalic12pt7b;
  expected.setTextWrap(false);
  expected.setFont(&font);
  expected.setTextColor(color);
  const char *labels[] = {"WATERCOLOR", "POP COMIC", "ANIME", "CLAY", "jgy"};
  for (const char *label : labels) {
    actual.fillScreen(0);
    expected.fillScreen(0);
    Rect box(20, 20, 280, 40);
    Text::drawFontIn(canvas, label, box, font, Color(color));
    int16_t left;
    int16_t top;
    uint16_t width;
    uint16_t height;
    expected.getTextBounds(label, 0, 0, &left, &top, &width, &height);
    expected.setCursor(box.x + (box.w - width) / 2 - left,
                       box.y + (box.h - height) / 2 - top);
    expected.print(label);
    assert(pixelCount(actual, box) > 50);
    samePixels(actual, expected);
  }
  for (const Rect box : {Rect(85, 80, 126, 22), Rect(25, 30, 48, 10),
                         Rect(60, 90, 200, 5), Rect(14, 17, 1, 1)}) {
    actual.fillScreen(0);
    Text::drawFontIn(canvas, "WATERCOLOR", box, font, Color(color));
    assert(pixelCount(actual, box) > 0);
    bool leftVisible = false;
    bool rightVisible = false;
    for (int16_t y = box.y; y < box.bottom(); ++y) {
      leftVisible |= actual.getPixel(box.x, y) != 0;
      rightVisible |= actual.getPixel(box.right() - 1, y) != 0;
    }
    if (box.w <= 126 && box.h >= 10) {
      assert(leftVisible && rightVisible);
    }
  }
  actual.fillScreen(0);
  canvas.setOrigin(15, -9);
  canvas.setClip(Rect(60, 45, 40, 8));
  Text::drawFontIn(canvas, "WATERCOLOR", Rect(20, 40, 126, 22), font, Color(color));
  assert(pixelCount(actual, canvas.clip()) > 0);
  canvas.setOrigin(0, 0);
  canvas.resetClip();
  expected.setFont(nullptr);
  for (const Align align : {Align::Left, Align::Center, Align::Right}) {
    actual.fillScreen(0);
    expected.fillScreen(0);
    Rect box(30, 70, 33, 12);
    Text::drawIn(canvas, "WATERCOLOR", box,
                 TextStyle(Color(color), 1, align, VAlign::Middle));
    int16_t x = align == Align::Left ? 30 : (align == Align::Center ? 31 : 33);
    expected.setCursor(x, 72);
    expected.print("WA...");
    assert(pixelCount(actual, box) > 0);
    samePixels(actual, expected);
  }
  for (const int16_t width : {0, 1, 5, 6, 12, 18}) {
    actual.fillScreen(0);
    expected.fillScreen(0);
    Rect box(50, 60, width, 8);
    Text::drawIn(canvas, "WATERCOLOR", box, TextStyle(Color(color), 1));
    expected.setCursor(50, 60);
    for (int16_t dot = 0; dot < width / 6; ++dot) {
      expected.write('.');
    }
    samePixels(actual, expected);
    const Size wrapped = Text::measureWrapped("cannot fit", 1, width);
    if (width < 6) {
      assert(wrapped.w == 0 && wrapped.h == 0);
      Text::drawWrapped(canvas, "cannot fit", box, TextStyle(Color(color), 1));
      assert(pixelCount(actual, box) == 0);
    }
  }
  actual.fillScreen(0);
  expected.fillScreen(0);
  Text::drawIn(canvas, "ANIME\nWATERCOLOR\nCLAY", Rect(20, 20, 60, 16),
               TextStyle(Color(color), 1));
  expected.setCursor(20, 20);
  expected.print("ANIME");
  expected.setCursor(20, 28);
  expected.print("WATERCO...");
  samePixels(actual, expected);
  actual.fillScreen(0);
  canvas.setOrigin(11, 13);
  canvas.setClip(Rect(33, 35, 20, 5));
  Text::drawIn(canvas, "WATERCOLOR", Rect(20, 20, 60, 16),
               TextStyle(Color(color), 1));
  assert(pixelCount(actual, canvas.clip()) > 0);
  canvas.setOrigin(0, 0);
  canvas.resetClip();
  actual.fillScreen(0);
  Text::drawWrapped(canvas, "one two three four", Rect(50, 50, 23, 18),
                    TextStyle(Color(color), 1, Align::Center, VAlign::Middle));
  assert(pixelCount(actual, Rect(50, 50, 23, 18)) > 0);
  assert(Text::glyphW(0) == 6 && Text::glyphH(0) == 8);
}
"""
        with tempfile.TemporaryDirectory(prefix="somnium-text-test-") as temporary:
            directory = Path(temporary)
            for name, content in headers.items():
                (directory / name).write_text(content)
            source = directory / "text.cpp"
            source.write_text(harness)
            executable = directory / "text"
            result = subprocess.run(
                [
                    "g++",
                    "-std=c++17",
                    "-DARDUINO=100",
                    "-I",
                    str(directory),
                    "-I",
                    str(GFX),
                    "-I",
                    str(ROOT / "lib/SomniumUi"),
                    str(source),
                    str(ROOT / "lib/SomniumUi/Text.cpp"),
                    str(GFX / "Adafruit_GFX.cpp"),
                    "-o",
                    str(executable),
                ],
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            subprocess.run([str(executable)], check=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
