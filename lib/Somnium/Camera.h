#pragma once

#include <Arduino.h>
#include <esp_camera.h>

namespace Somnium {

struct JpegBuffer {
  uint8_t *data = nullptr;
  size_t length = 0;
  uint32_t crc32 = 0;
  unsigned long captureMs = 0;
  unsigned long encodeMs = 0;
  int width = 0;
  int height = 0;
};

class Camera {
 public:
  using FrameCallback = void (*)(const camera_fb_t *frame);

  static bool begin(int width = 320);
  // Power down the sensor (call after a snap; next capture re-inits).
  static void end();
  static bool ready();
  static int activeWidth();
  static bool captureJpeg(JpegBuffer &out, int width = 320,
                          int jpegQuality = 70);
  static void release(JpegBuffer &buffer);
  static void setFrameCallback(FrameCallback callback);

 private:
  static bool configure(int width);

  static SemaphoreHandle_t sMutex;
  static int sActiveWidth;
  static FrameCallback sFrameCallback;
};

}  // namespace Somnium
