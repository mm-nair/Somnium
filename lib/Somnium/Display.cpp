#include "Display.h"

#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <SPI.h>

#include "Camera.h"
#include "Config.h"
#include "Console.h"

namespace Somnium {
namespace {

Adafruit_ILI9341 *gTft = nullptr;
bool gReady = false;

}  // namespace

bool Display::begin() {
  if (gReady) {
    return true;
  }
  SPI.begin(Pins::kDisplaySck, -1, Pins::kDisplayMosi, Pins::kDisplayCs);
  gTft = new Adafruit_ILI9341(&SPI, Pins::kDisplayDc, Pins::kDisplayCs,
                              Pins::kDisplayRst);
  gTft->begin(Defaults::kDisplaySpiHz);
  gTft->setRotation(1);
  gTft->fillScreen(0x0000);
  gReady = true;
  Camera::setFrameCallback(onCameraFrame);
  Console::println("LCD ready");
  return true;
}

void Display::clear(uint16_t color) {
  if (gReady && gTft != nullptr) {
    gTft->fillScreen(color);
  }
}

int Display::width() { return Defaults::kDisplayWidth; }

int Display::height() { return Defaults::kDisplayHeight; }

bool Display::ready() { return gReady && gTft != nullptr; }

Adafruit_ILI9341 *Display::tft() { return gTft; }

void Display::drawRgb565(const uint16_t *pixels, int width, int height) {
  if (!gReady || gTft == nullptr || pixels == nullptr ||
      width != Defaults::kDisplayWidth || height != Defaults::kDisplayHeight) {
    return;
  }
  gTft->startWrite();
  gTft->setAddrWindow(0, 0, width, height);
  gTft->writePixels(const_cast<uint16_t *>(pixels),
                    static_cast<uint32_t>(width) * height);
  gTft->endWrite();
}

void Display::drawCameraFrame(const camera_fb_t *frame) {
  if (!gReady || gTft == nullptr || frame == nullptr ||
      frame->format != PIXFORMAT_RGB565 ||
      (frame->width != 320 && frame->width != 640) ||
      frame->height != frame->width * 3 / 4 ||
      frame->len != frame->width * frame->height * 2) {
    return;
  }

  uint16_t row[Defaults::kDisplayWidth];
  const size_t scale = frame->width / Defaults::kDisplayWidth;
  gTft->startWrite();
  gTft->setAddrWindow(0, 0, Defaults::kDisplayWidth, Defaults::kDisplayHeight);
  for (size_t y = 0; y < static_cast<size_t>(Defaults::kDisplayHeight); ++y) {
    const uint8_t *src = frame->buf + y * scale * frame->width * 2;
    for (size_t x = 0; x < static_cast<size_t>(Defaults::kDisplayWidth); ++x) {
      const size_t offset = x * scale * 2;
      row[x] = (static_cast<uint16_t>(src[offset]) << 8) | src[offset + 1];
    }
    gTft->writePixels(row, Defaults::kDisplayWidth);
  }
  gTft->endWrite();
}

void Display::onCameraFrame(const camera_fb_t *frame) { drawCameraFrame(frame); }

}  // namespace Somnium
