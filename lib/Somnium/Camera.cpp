#include "Camera.h"

#include <esp_rom_crc.h>
#include <img_converters.h>

#include "Config.h"
#include "Console.h"

namespace Somnium {

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
  if (frame->width != static_cast<size_t>(width) ||
      frame->len != frame->width * frame->height * 2) {
    esp_camera_fb_return(frame);
    xSemaphoreGive(sMutex);
    return false;
  }

  const unsigned long encodeStarted = millis();
  const bool encoded = frame2jpg(frame, jpegQuality, &out.data, &out.length);
  out.encodeMs = millis() - encodeStarted;
  if (encoded && sFrameCallback != nullptr) {
    sFrameCallback(frame);
  }
  out.width = static_cast<int>(frame->width);
  out.height = static_cast<int>(frame->height);
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
