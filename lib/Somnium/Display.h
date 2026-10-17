#pragma once

#include <Arduino.h>
#include <esp_camera.h>

namespace Somnium {

// Thin SPI LCD driver (Adafruit ILI9341). No UI chrome — just init + blit.
class Display {
 public:
  static bool begin();
  static void clear(uint16_t color = 0x0000);

  // Blit camera RGB565 frame to full screen (QVGA 1:1, VGA 2:1 downsample).
  static void drawCameraFrame(const camera_fb_t *frame);

  // Blit a packed RGB565 buffer already sized to the panel (320x240).
  static void drawRgb565(const uint16_t *pixels, int width, int height);

  static int width();
  static int height();

  // Registered with Camera::setFrameCallback from begin() when successful.
  static void onCameraFrame(const camera_fb_t *frame);
};

}  // namespace Somnium
