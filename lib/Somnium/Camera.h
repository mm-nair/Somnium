#pragma once

#include <Arduino.h>
#include <esp_camera.h>

namespace Somnium {

// Owned JPEG buffer in heap/PSRAM from frame2jpg.
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
  // Initializes GC0308 (Freenove) at QVGA (320) or VGA (640).
  static bool begin(int width = 320);

  static bool ready();
  static int activeWidth();

  // Captures one fresh RGB565 frame, optionally draws it via Display hook,
  // encodes JPEG, and fills out. Caller must release(out).
  static bool captureJpeg(JpegBuffer &out, int width = 320,
                          int jpegQuality = 70);

  static void release(JpegBuffer &buffer);

  // Optional hook called under the camera mutex with the raw RGB565 frame
  // after a successful JPEG encode (used to push the shot to the LCD).
  using FrameCallback = void (*)(const camera_fb_t *frame);
  static void setFrameCallback(FrameCallback callback);

 private:
  static bool configure(int width);

  static SemaphoreHandle_t sMutex;
  static int sActiveWidth;
  static FrameCallback sFrameCallback;
};

}  // namespace Somnium
