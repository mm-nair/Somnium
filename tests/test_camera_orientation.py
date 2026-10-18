from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class CameraOrientationTests(unittest.TestCase):
    def test_reversible_landscape_orientation_and_capture(self):
        headers = {
            "Arduino.h": r"""
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
using SemaphoreHandle_t = void *;
inline unsigned long millis() { return 0; }
inline void yield() {}
inline void *ps_malloc(size_t length) { return malloc(length); }
inline bool psramFound() { return true; }
inline SemaphoreHandle_t xSemaphoreCreateMutex() { return reinterpret_cast<void *>(1); }
inline bool xSemaphoreTake(SemaphoreHandle_t, unsigned long) { return true; }
inline void xSemaphoreGive(SemaphoreHandle_t) {}
#define pdMS_TO_TICKS(value) (value)
#define pdTRUE true
""",
            "esp_camera.h": r"""
#pragma once
#include "Arduino.h"
using esp_err_t = int;
constexpr int ESP_OK = 0;
constexpr int LEDC_CHANNEL_0 = 0;
constexpr int LEDC_TIMER_0 = 0;
constexpr int PIXFORMAT_RGB565 = 0;
constexpr int FRAMESIZE_QVGA = 0;
constexpr int FRAMESIZE_VGA = 1;
constexpr int CAMERA_FB_IN_DRAM = 0;
constexpr int CAMERA_FB_IN_PSRAM = 1;
constexpr int CAMERA_GRAB_WHEN_EMPTY = 0;
struct camera_config_t {
  int ledc_channel, ledc_timer;
  int pin_d0, pin_d1, pin_d2, pin_d3, pin_d4, pin_d5, pin_d6, pin_d7;
  int pin_xclk, pin_pclk, pin_vsync, pin_href, pin_sccb_sda, pin_sccb_scl;
  int pin_pwdn, pin_reset, xclk_freq_hz, pixel_format, frame_size, fb_count;
  int fb_location, grab_mode;
};
struct camera_fb_t {
  uint8_t *buf = nullptr;
  size_t len = 0;
  size_t width = 0;
  size_t height = 0;
  int format = PIXFORMAT_RGB565;
};
struct sensor_t {
  struct { uint16_t PID = 0x9b; } id;
};
inline camera_fb_t *testFrame = nullptr;
inline sensor_t testSensor;
inline int testReturnedFrames = 0;
inline esp_err_t esp_camera_init(camera_config_t *) { return ESP_OK; }
inline esp_err_t esp_camera_deinit() { return ESP_OK; }
inline sensor_t *esp_camera_sensor_get() { return &testSensor; }
inline camera_fb_t *esp_camera_fb_get() { return testFrame; }
inline void esp_camera_fb_return(camera_fb_t *) { ++testReturnedFrames; }
""",
            "esp_rom_crc.h": r"""
#pragma once
#include "Arduino.h"
inline uint32_t esp_rom_crc32_le(uint32_t, const uint8_t *, size_t) {
  return 0x12345678;
}
""",
            "img_converters.h": r"""
#pragma once
#include "esp_camera.h"
bool frame2jpg(camera_fb_t *, int, uint8_t **, size_t *);
""",
        }
        harness = r"""
#include <cassert>
#include <vector>
#include "Camera.cpp"

static std::vector<uint8_t> previewPixels;
static std::vector<uint8_t> encodedPixels;

namespace Somnium {
void Console::println(const char *) {}
void Console::printf(const char *, ...) {}
}

bool frame2jpg(camera_fb_t *frame, int, uint8_t **data, size_t *length) {
  encodedPixels.assign(frame->buf, frame->buf + frame->len);
  *length = 4;
  *data = static_cast<uint8_t *>(malloc(*length));
  memcpy(*data, "\xff\xd8\xff\xd9", *length);
  return true;
}

static void capturePreview(const camera_fb_t *frame) {
  previewPixels.assign(frame->buf, frame->buf + frame->len);
}

static void verifyOrientation(size_t width, size_t height) {
  std::vector<uint8_t> sourcePixels(width * height * 2);
  for (size_t y = 0; y < height; ++y) {
    for (size_t x = 0; x < width; ++x) {
      const uint16_t pixel = static_cast<uint16_t>(0x1234 + x * 197 + y * 19);
      sourcePixels[(y * width + x) * 2] = pixel >> 8;
      sourcePixels[(y * width + x) * 2 + 1] = pixel & 0xff;
    }
  }
  const std::vector<uint8_t> originalPixels = sourcePixels;
  camera_fb_t frame{sourcePixels.data(), sourcePixels.size(), width, height};
  camera_fb_t landscapeFrame;
  camera_fb_t mirroredFrame;
  camera_fb_t defaultFrame;
  assert(Somnium::orientLandscapeFrame(frame, landscapeFrame, false));
  assert(Somnium::orientLandscapeFrame(frame, mirroredFrame, true));
  assert(Somnium::orientLandscapeFrame(frame, defaultFrame));
  assert(landscapeFrame.width == width && landscapeFrame.height == height);
  assert(mirroredFrame.width == width && mirroredFrame.height == height);
  assert(mirroredFrame.len == sourcePixels.size());

  std::vector<uint8_t> portraitPixels(sourcePixels.size());
  for (size_t y = 0; y < height; ++y) {
    for (size_t x = 0; x < width; ++x) {
      const size_t portraitPixel = (width - 1 - x) * height + y;
      memcpy(&portraitPixels[portraitPixel * 2],
             &sourcePixels[(y * width + x) * 2], 2);
    }
  }
  const size_t cropHeight = height * height / width;
  const size_t cropTop = width - (width - cropHeight) / 2 - cropHeight;
  for (size_t y = 0; y < height; ++y) {
    for (size_t x = 0; x < width; ++x) {
      const size_t portraitX = static_cast<size_t>((x + 0.5) * height / width);
      const size_t portraitY = cropTop +
          static_cast<size_t>((y + 0.5) * cropHeight / height);
      const size_t expectedOffset = (portraitY * height + portraitX) * 2;
      const size_t outputOffset = (y * width + x) * 2;
      assert(memcmp(landscapeFrame.buf + outputOffset,
                    &portraitPixels[expectedOffset], 2) == 0);
      const size_t mirroredOffset = (y * width + width - 1 - x) * 2;
      assert(memcmp(mirroredFrame.buf + mirroredOffset,
                    landscapeFrame.buf + outputOffset, 2) == 0);
    }
  }
  assert(memcmp(defaultFrame.buf,
                Somnium::Defaults::kCameraMirrorHorizontal ? mirroredFrame.buf
                                                           : landscapeFrame.buf,
                frame.len) == 0);
  assert(sourcePixels == originalPixels);
  assert(memcmp(mirroredFrame.buf, landscapeFrame.buf, frame.len) != 0);

  if (width == 320 || width == 640) {
    testFrame = &frame;
    testReturnedFrames = 0;
    assert(Somnium::Camera::begin(static_cast<int>(width)));
    Somnium::Camera::setFrameCallback(capturePreview);
    Somnium::JpegBuffer jpeg;
    assert(Somnium::Camera::captureJpeg(jpeg, static_cast<int>(width)));
    assert(jpeg.width == static_cast<int>(width));
    assert(jpeg.height == static_cast<int>(height));
    assert(testReturnedFrames == 2);
    assert(previewPixels.size() == frame.len);
    assert(memcmp(previewPixels.data(), defaultFrame.buf, frame.len) == 0);
    assert(encodedPixels == previewPixels);
    assert(sourcePixels == originalPixels);
    Somnium::Camera::release(jpeg);
    Somnium::Camera::end();
  }
  free(landscapeFrame.buf);
  free(mirroredFrame.buf);
  free(defaultFrame.buf);
}

int main() {
  verifyOrientation(8, 6);
  verifyOrientation(320, 240);
  verifyOrientation(640, 480);
  return 0;
}
"""
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            for name, contents in headers.items():
                (directory / name).write_text(contents)
            (directory / "harness.cpp").write_text(harness)
            executable = directory / "camera_orientation_test"
            compilation = subprocess.run(
                [
                    "g++",
                    "-std=c++17",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    f"-I{directory}",
                    f"-I{ROOT / 'lib/Somnium'}",
                    str(directory / "harness.cpp"),
                    "-o",
                    str(executable),
                ],
                capture_output=True,
                text=True,
            )
            self.assertEqual(compilation.returncode, 0, compilation.stderr)
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    unittest.main()
