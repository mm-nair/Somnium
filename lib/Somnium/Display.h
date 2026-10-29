#pragma once

#include <Arduino.h>
#include <Adafruit_ILI9341.h>
#include <esp_camera.h>

namespace Somnium {

class Display {
 public:
  static bool begin(bool bootColors = true);
  // Blank the panel and put the ILI9341 to sleep. There is no backlight
  // GPIO in the pin map; this is the software-off path. Returns false if
  // begin() never created the panel.
  static bool shutdown();
  static void clear(uint16_t color = 0x0000);
  static void drawCameraFrame(const camera_fb_t *frame);
  static void drawRgb565(const uint16_t *pixels, int width, int height);
  static int width();
  static int height();
  static bool ready();
  static Adafruit_ILI9341 *tft();
  static void onCameraFrame(const camera_fb_t *frame);
};

}  // namespace Somnium
