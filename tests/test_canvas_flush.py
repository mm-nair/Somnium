from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class CanvasFlushTests(unittest.TestCase):
    def test_completed_frames_match_full_redraws_with_partial_transfers(self):
        headers = {
            "Arduino.h": r"""
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
""",
            "esp_heap_caps.h": r"""
#pragma once
#include "Arduino.h"
inline bool testFailFramebuffer = false;
inline bool testFailHistory = false;
inline size_t testAllocationCount = 0;
inline void *heap_caps_malloc(size_t length, int) {
  ++testAllocationCount;
  if (testFailFramebuffer || (testFailHistory && testAllocationCount == 2)) {
    return nullptr;
  }
  return std::malloc(length);
}
inline void *testMalloc(size_t length) {
  return testFailFramebuffer ? nullptr : std::malloc(length);
}
#define malloc testMalloc
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
""",
            "Adafruit_GFX.h": r"""
#pragma once
#include "Arduino.h"
class Adafruit_GFX {
 public:
  Adafruit_GFX(int16_t width, int16_t height) : _width(width), _height(height) {}
  virtual ~Adafruit_GFX() = default;
  virtual void drawPixel(int16_t x, int16_t y, uint16_t color) = 0;
  virtual void fillScreen(uint16_t color) {
    fillRect(0, 0, _width, _height, color);
  }
  void fillRect(int16_t x, int16_t y, int16_t width, int16_t height,
                uint16_t color) {
    for (int16_t row = y; row < y + height; ++row) {
      for (int16_t column = x; column < x + width; ++column) {
        drawPixel(column, row, color);
      }
    }
  }
  void drawRect(int16_t x, int16_t y, int16_t width, int16_t height,
                uint16_t color) {
    fillRect(x, y, width, 1, color);
    fillRect(x, y + height - 1, width, 1, color);
    fillRect(x, y, 1, height, color);
    fillRect(x + width - 1, y, 1, height, color);
  }
 protected:
  int16_t _width;
  int16_t _height;
};
""",
            "Adafruit_SPITFT.h": r"""
#pragma once
#include "Adafruit_GFX.h"
#include <cassert>
#include <vector>
class Adafruit_SPITFT : public Adafruit_GFX {
 public:
  Adafruit_SPITFT(int16_t width, int16_t height)
      : Adafruit_GFX(width, height), pixels(width * height, 0xffff) {}
  void drawPixel(int16_t x, int16_t y, uint16_t color) override {
    if (x >= 0 && x < _width && y >= 0 && y < _height) {
      pixels[y * _width + x] = color;
      ++directPixelCount;
    }
  }
  void startWrite() {
    assert(!writing);
    writing = true;
    ++transactionCount;
  }
  void endWrite() {
    assert(writing);
    assert(windowOffset == static_cast<size_t>(windowWidth) * windowHeight);
    writing = false;
  }
  void setAddrWindow(uint16_t x, uint16_t y, uint16_t width, uint16_t height) {
    assert(writing);
    assert(windowOffset == static_cast<size_t>(windowWidth) * windowHeight);
    assert(width > 0 && height > 0 && x + width <= _width && y + height <= _height);
    windowX = x;
    windowY = y;
    windowWidth = width;
    windowHeight = height;
    windowOffset = 0;
    ++windowCount;
  }
  void writePixels(uint16_t *colors, uint32_t length, bool block = true,
                   bool bigEndian = false) {
    assert(writing && block && !bigEndian);
    assert(windowOffset + length <= static_cast<size_t>(windowWidth) * windowHeight);
    for (uint32_t i = 0; i < length; ++i) {
      const size_t x = windowX + windowOffset % windowWidth;
      const size_t y = windowY + windowOffset / windowWidth;
      pixels[y * _width + x] = colors[i];
      ++windowOffset;
    }
    transferredPixelCount += length;
  }
  void drawRGBBitmap(int16_t x, int16_t y, uint16_t *colors, int16_t width,
                     int16_t height) {
    startWrite();
    setAddrWindow(x, y, width, height);
    for (int16_t row = 0; row < height; ++row) {
      writePixels(colors + row * width, width);
    }
    endWrite();
  }
  void resetCounters() {
    transactionCount = 0;
    windowCount = 0;
    transferredPixelCount = 0;
    directPixelCount = 0;
  }
  std::vector<uint16_t> pixels;
  size_t transactionCount = 0;
  size_t windowCount = 0;
  size_t transferredPixelCount = 0;
  size_t directPixelCount = 0;
 private:
  bool writing = false;
  uint16_t windowX = 0;
  uint16_t windowY = 0;
  uint16_t windowWidth = 0;
  uint16_t windowHeight = 0;
  size_t windowOffset = 0;
};
""",
        }
        harness = r"""
#include "Canvas.h"
#include "esp_heap_caps.h"
#include <algorithm>
#include <cassert>
#include <vector>

int main() {
  using namespace Somnium::Ui;
  constexpr int16_t width = 320;
  constexpr int16_t height = 240;
  constexpr size_t pixelCount = width * height;
  Adafruit_SPITFT display(width, height);
  Canvas canvas;
  canvas.bind(&display, width, height);
  std::vector<uint16_t> expected(pixelCount, 0x1234);
  const uint16_t colors[] = {0xf800, 0x07e0, 0x001f, 0xaaaa, 0x5555, 0xffff};
  auto drawInitial = [&]() {
    canvas.clear(Color(0x1234));
    canvas.blitRgb565(9, 12, 3, 2, colors);
  };
  for (int row = 0; row < 2; ++row) {
    for (int column = 0; column < 3; ++column) {
      expected[(12 + row) * width + 9 + column] = colors[row * 3 + column];
    }
  }
  canvas.beginFrame();
  drawInitial();
  canvas.endFrame();
  assert(display.pixels == expected);
  assert(display.transferredPixelCount == pixelCount);
  assert(display.transactionCount == 1 && display.windowCount == 1);

  display.resetCounters();
  canvas.endFrame();
  canvas.beginFrame();
  drawInitial();
  canvas.endFrame();
  assert(display.pixels == expected);
  assert(display.transferredPixelCount == 0);
  assert(display.transactionCount == 0 && display.windowCount == 0);

  canvas.beginFrame();
  canvas.fillRect(Rect(101, 65, 3, 2), Color(0x3456));
  for (int row = 65; row < 67; ++row) {
    for (int column = 101; column < 104; ++column) {
      expected[row * width + column] = 0x3456;
    }
  }
  canvas.endFrame();
  assert(display.pixels == expected);
  assert(display.transferredPixelCount == 6);
  assert(display.transactionCount == 1 && display.windowCount == 1);

  display.resetCounters();
  canvas.beginFrame();
  canvas.gfx()->drawPixel(0, 0, 0xabcd);
  canvas.gfx()->drawPixel(width - 1, 0, 0x2468);
  canvas.gfx()->drawPixel(5, 1, 0x1357);
  canvas.gfx()->drawPixel(5, 3, 0x1357);
  canvas.gfx()->drawPixel(width - 1, height - 1, 0xbeef);
  expected[0] = 0xabcd;
  expected[width - 1] = 0x2468;
  expected[width + 5] = 0x1357;
  expected[width * 3 + 5] = 0x1357;
  expected.back() = 0xbeef;
  canvas.endFrame();
  assert(display.pixels == expected);
  assert(display.transferredPixelCount == 5);
  assert(display.transactionCount == 1 && display.windowCount == 5);

  uint32_t randomState = 1;
  for (int frame = 0; frame < 100; ++frame) {
    display.resetCounters();
    canvas.beginFrame();
    for (int change = 0; change < 250; ++change) {
      randomState = randomState * 1664525u + 1013904223u;
      const size_t offset = randomState % pixelCount;
      randomState = randomState * 1664525u + 1013904223u;
      const uint16_t color = randomState >> 16;
      canvas.gfx()->drawPixel(offset % width, offset / width, color);
      expected[offset] = color;
    }
    canvas.endFrame();
    assert(display.pixels == expected);
    assert(display.transferredPixelCount <= pixelCount);
  }

  display.resetCounters();
  canvas.beginFrame();
  for (size_t offset = 0; offset < pixelCount; ++offset) {
    expected[offset] ^= 0xffff;
  }
  canvas.blitRgb565(0, 0, width, height, expected.data());
  canvas.endFrame();
  assert(display.pixels == expected);
  assert(display.transferredPixelCount == pixelCount);
  assert(display.transactionCount == 1 && display.windowCount == 1);

  display.resetCounters();
  canvas.beginFrame();
  canvas.setOrigin(10, 20);
  canvas.setClip(Rect(11, 22, 2, 1));
  canvas.blitRgb565(0, 1, 3, 2, colors);
  expected[22 * width + 11] = colors[4];
  expected[22 * width + 12] = colors[5];
  canvas.endFrame();
  assert(display.pixels == expected);
  assert(display.transferredPixelCount == 2);

  auto drawPanels = [&](int16_t horizontalOffset) {
    canvas.clear(Color(0));
    for (int16_t panel = 0; panel < 3; ++panel) {
      canvas.fillRect(Rect(5 + horizontalOffset + panel * 100, 40, 90, 140),
                      Color(colors[panel]));
    }
  };
  canvas.beginFrame();
  drawPanels(0);
  canvas.endFrame();
  display.resetCounters();
  canvas.beginFrame();
  drawPanels(1);
  canvas.endFrame();
  expected.assign(pixelCount, 0);
  for (int16_t panel = 0; panel < 3; ++panel) {
    for (int16_t row = 40; row < 180; ++row) {
      for (int16_t column = 6 + panel * 100; column < 96 + panel * 100; ++column) {
        expected[row * width + column] = colors[panel];
      }
    }
  }
  assert(display.pixels == expected);
  assert(display.transferredPixelCount == 840);
  assert(display.transactionCount == 1);

  canvas.beginFrame();
  canvas.clear(Color(0));
  canvas.endFrame();
  display.resetCounters();
  canvas.beginFrame();
  canvas.setClip(Rect(10, 10, 4, 4));
  canvas.drawRect(Rect(8, 8, 5, 5), Color(0xffff));
  canvas.endFrame();
  expected.assign(pixelCount, 0);
  for (int16_t coordinate = 10; coordinate <= 12; ++coordinate) {
    expected[coordinate * width + 12] = 0xffff;
    expected[12 * width + coordinate] = 0xffff;
  }
  assert(display.pixels == expected);

  testAllocationCount = 0;
  testFailHistory = true;
  canvas.bind(&display, width, height);
  for (int frame = 0; frame < 2; ++frame) {
    display.resetCounters();
    canvas.beginFrame();
    canvas.clear(Color(0x4321));
    canvas.endFrame();
    expected.assign(pixelCount, 0x4321);
    assert(display.pixels == expected);
    assert(display.transferredPixelCount == pixelCount);
  }

  testAllocationCount = 0;
  testFailHistory = false;
  canvas.bind(&display, width, height);
  display.resetCounters();
  canvas.beginFrame();
  canvas.clear(Color(0x4321));
  canvas.endFrame();
  assert(display.pixels == expected);
  assert(display.transferredPixelCount == pixelCount);

  testAllocationCount = 0;
  testFailFramebuffer = true;
  canvas.bind(&display, width, height);
  display.resetCounters();
  canvas.beginFrame();
  canvas.clear(Color(0x5432));
  canvas.endFrame();
  expected.assign(pixelCount, 0x5432);
  assert(display.pixels == expected);
  assert(display.directPixelCount == pixelCount);
  assert(display.transferredPixelCount == 0);

  canvas.bind(nullptr, width, height);
  canvas.beginFrame();
  canvas.clear(Color(0x6543));
  canvas.endFrame();
  return 0;
}
"""
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            for name, contents in headers.items():
                (directory / name).write_text(contents)
            harness_path = directory / "canvas_flush.cpp"
            harness_path.write_text(harness)
            executable = directory / "canvas_flush"
            subprocess.run(
                [
                    "g++",
                    "-std=c++17",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(directory),
                    "-I",
                    str(ROOT / "lib/SomniumUi"),
                    str(harness_path),
                    str(ROOT / "lib/SomniumUi/Canvas.cpp"),
                    "-o",
                    str(executable),
                ],
                check=True,
            )
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    unittest.main()
