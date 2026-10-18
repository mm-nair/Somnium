#include "Camera.h"

#include <esp_rom_crc.h>
#include <img_converters.h>

#include "Config.h"
#include "Console.h"

namespace Somnium {
namespace {

bool orientLandscapeFrame(const camera_fb_t &frame, camera_fb_t &landscapeFrame,
                          bool mirrorHorizontal =
                              Defaults::kCameraMirrorHorizontal) {
  landscapeFrame = frame;
  landscapeFrame.buf = static_cast<uint8_t *>(ps_malloc(frame.len));
  if (landscapeFrame.buf == nullptr) {
    landscapeFrame.buf = static_cast<uint8_t *>(malloc(frame.len));
  }
  if (landscapeFrame.buf == nullptr) {
    return false;
  }

  const size_t cropWidth = frame.height * frame.height / frame.width;
  const size_t cropLeft = (frame.width - cropWidth) / 2;
  for (size_t y = 0; y < frame.height; ++y) {
    const size_t sourceX = cropLeft + cropWidth - 1 -
                           (2 * y + 1) * cropWidth / (2 * frame.height);
    for (size_t x = 0; x < frame.width; ++x) {
      const size_t sourceY = (2 * x + 1) * frame.height / (2 * frame.width);
      const size_t sourceOffset = (sourceY * frame.width + sourceX) * 2;
      const size_t outputX = mirrorHorizontal ? frame.width - 1 - x : x;
      const size_t outputOffset = (y * frame.width + outputX) * 2;
      landscapeFrame.buf[outputOffset] = frame.buf[sourceOffset];
      landscapeFrame.buf[outputOffset + 1] = frame.buf[sourceOffset + 1];
    }
    if ((y & 15) == 0) {
      yield();
    }
  }
  return true;
}

}

SemaphoreHandle_t Camera::sMutex = nullptr;
int Camera::sActiveWidth = 0;
Camera::FrameCallback Camera::sFrameCallback = nullptr;

bool Camera::configure(int width) {
  if (width != 320 && width != 640) {
    return false;
  }
  if (!psramFound()) {
    Console::println("ERR no_psram");
    return false;
  }

  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Pins::kCamD0;
  config.pin_d1 = Pins::kCamD1;
  config.pin_d2 = Pins::kCamD2;
  config.pin_d3 = Pins::kCamD3;
  config.pin_d4 = Pins::kCamD4;
  config.pin_d5 = Pins::kCamD5;
  config.pin_d6 = Pins::kCamD6;
  config.pin_d7 = Pins::kCamD7;
  config.pin_xclk = Pins::kCamXclk;
  config.pin_pclk = Pins::kCamPclk;
  config.pin_vsync = Pins::kCamVsync;
  config.pin_href = Pins::kCamHref;
  config.pin_sccb_sda = Pins::kCamSda;
  config.pin_sccb_scl = Pins::kCamScl;
  config.pin_pwdn = Pins::kCamPwdn;
  config.pin_reset = Pins::kCamReset;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_RGB565;
  config.frame_size = width == 320 ? FRAMESIZE_QVGA : FRAMESIZE_VGA;
  config.fb_count = 1;
  config.fb_location = width == 320 ? CAMERA_FB_IN_DRAM : CAMERA_FB_IN_PSRAM;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

  const esp_err_t result = esp_camera_init(&config);
  if (result != ESP_OK) {
    sActiveWidth = 0;
    Console::printf("ERR camera_init 0x%x\n", result);
    return false;
  }
  sActiveWidth = width;
  return true;
}

bool Camera::begin(int width) {
  if (sMutex == nullptr) {
    sMutex = xSemaphoreCreateMutex();
  }
  if (sMutex == nullptr || !configure(width)) {
    return false;
  }
  Console::printf("CAM %dx%d pid=0x%04x\n", width, width * 3 / 4,
                  esp_camera_sensor_get()->id.PID);
  return true;
}

void Camera::end() {
  if (sMutex == nullptr) {
    return;
  }
  if (xSemaphoreTake(sMutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
    return;
  }
  if (sActiveWidth != 0) {
    esp_camera_deinit();
    sActiveWidth = 0;
    Console::println("CAM off");
  }
  xSemaphoreGive(sMutex);
}

bool Camera::ready() { return sActiveWidth != 0; }

int Camera::activeWidth() { return sActiveWidth; }

void Camera::setFrameCallback(FrameCallback callback) {
  sFrameCallback = callback;
}

bool Camera::captureJpeg(JpegBuffer &out, int width, int jpegQuality) {
  out = JpegBuffer{};
  if (sMutex == nullptr || (width != 320 && width != 640) || jpegQuality < 1 ||
      jpegQuality > 95 ||
      xSemaphoreTake(sMutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
    return false;
  }

  if (sActiveWidth != width) {
    if (sActiveWidth != 0) {
      esp_camera_deinit();
      sActiveWidth = 0;
    }
    if (!configure(width)) {
      xSemaphoreGive(sMutex);
      return false;
    }
  }

  const unsigned long captureStarted = millis();
  camera_fb_t *frame = esp_camera_fb_get();
  if (frame != nullptr) {
    esp_camera_fb_return(frame);
    frame = esp_camera_fb_get();
  }
  out.captureMs = millis() - captureStarted;
  if (frame == nullptr) {
    xSemaphoreGive(sMutex);
    return false;
  }
  if (frame->buf == nullptr || frame->format != PIXFORMAT_RGB565 ||
      frame->width != static_cast<size_t>(width) ||
      frame->height != static_cast<size_t>(width * 3 / 4) ||
      frame->len != frame->width * frame->height * 2) {
    esp_camera_fb_return(frame);
    xSemaphoreGive(sMutex);
    return false;
  }

  const unsigned long encodeStarted = millis();
  camera_fb_t landscapeFrame{};
  if (!orientLandscapeFrame(*frame, landscapeFrame)) {
    esp_camera_fb_return(frame);
    xSemaphoreGive(sMutex);
    return false;
  }

  if (sFrameCallback != nullptr) {
    sFrameCallback(&landscapeFrame);
  }

  const bool encoded =
      frame2jpg(&landscapeFrame, jpegQuality, &out.data, &out.length);
  out.encodeMs = millis() - encodeStarted;
  out.width = static_cast<int>(landscapeFrame.width);
  out.height = static_cast<int>(landscapeFrame.height);
  free(landscapeFrame.buf);
  esp_camera_fb_return(frame);
  xSemaphoreGive(sMutex);

  if (!encoded) {
    free(out.data);
    out = JpegBuffer{};
    return false;
  }
  out.crc32 = esp_rom_crc32_le(0, out.data, out.length);
  return true;
}

void Camera::release(JpegBuffer &buffer) {
  free(buffer.data);
  buffer = JpegBuffer{};
}

}  // namespace Somnium
