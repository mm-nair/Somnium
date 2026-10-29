#include "Camera.h"

#include <string.h>

#include <esp_rom_crc.h>
#include <ESP32_OV5640_AF.h>

#include "Config.h"
#include "Console.h"
#include "Power.h"

namespace Somnium {
namespace {

OV5640 gAf;
bool gAfReady = false;

framesize_t frameForWidth(int width) {
  if (width <= 320) {
    return FRAMESIZE_QVGA;
  }
  if (width <= 640) {
    return FRAMESIZE_VGA;
  }
  if (width <= 800) {
    return FRAMESIZE_SVGA;
  }
  if (width <= 1024) {
    return FRAMESIZE_XGA;
  }
  if (width <= 1280) {
    return FRAMESIZE_SXGA;
  }
  return FRAMESIZE_SVGA;
}

}  // namespace

SemaphoreHandle_t Camera::sMutex = nullptr;
int Camera::sActiveWidth = 0;
Camera::FrameCallback Camera::sFrameCallback = nullptr;

bool Camera::configure(int width) {
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
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = frameForWidth(width);
  config.jpeg_quality = Defaults::kJpegQuality;
  config.fb_count = 1;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

  const esp_err_t result = esp_camera_init(&config);
  if (result != ESP_OK) {
    sActiveWidth = 0;
    Console::printf("ERR camera_init 0x%x\n", result);
    return false;
  }

  sensor_t *sensor = esp_camera_sensor_get();
  if (sensor != nullptr) {
    sensor->set_framesize(sensor, frameForWidth(width));
    sensor->set_quality(sensor, Defaults::kJpegQuality);
    sensor->set_hmirror(sensor, Defaults::kCameraMirrorHorizontal ? 1 : 0);
    sensor->set_vflip(sensor, 0);
  }

  sActiveWidth = width;
  gAfReady = false;
  return true;
}

void Camera::primeFocus() {
  if (gAfReady || sActiveWidth == 0) {
    return;
  }
  sensor_t *sensor = esp_camera_sensor_get();
  if (sensor == nullptr) {
    return;
  }
  if (!gAf.start(sensor)) {
    Console::println("CAM AF sensor is not OV5640");
    return;
  }
  const uint8_t loaded = gAf.focusInit();
  const uint8_t mode = loaded == 0 ? gAf.autoFocusMode() : loaded;
  gAfReady = loaded == 0 && mode == 0;
  Console::printf("CAM AF %s fw=%u mode=%u\n", gAfReady ? "on" : "off", loaded,
                  mode);
}

bool Camera::begin(int width) {
  if (sMutex == nullptr) {
    sMutex = xSemaphoreCreateMutex();
  }
  if (sMutex == nullptr || !configure(width)) {
    return false;
  }
  const sensor_t *sensor = esp_camera_sensor_get();
  const unsigned pid = sensor != nullptr ? sensor->id.PID : 0;
  Console::printf("CAM jpeg %d pid=0x%04x\n", width, pid);
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
    gAfReady = false;
    Console::println("CAM off");
  }
  xSemaphoreGive(sMutex);
}

bool Camera::ready() { return sActiveWidth != 0; }

int Camera::activeWidth() { return sActiveWidth; }

void Camera::setFrameCallback(FrameCallback callback) { sFrameCallback = callback; }

bool Camera::captureJpeg(JpegBuffer &out, int width, int jpegQuality) {
  PowerBusy busy;
  out = JpegBuffer{};
  if (sMutex == nullptr || jpegQuality < 1 || jpegQuality > 100 ||
      xSemaphoreTake(sMutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
    return false;
  }
  // esp_camera quality is 0..63 and smaller means a larger file. Values
  // above 63 are the old 1..100 "higher is better" scale from USB capture.
  int sensorQuality = jpegQuality;
  if (sensorQuality > 63) {
    sensorQuality = 63 - (sensorQuality * 63 / 100);
  }
  if (sensorQuality < 4) {
    sensorQuality = 4;
  }

  if (sActiveWidth == 0) {
    if (!configure(width)) {
      xSemaphoreGive(sMutex);
      return false;
    }
  }

  sensor_t *sensor = esp_camera_sensor_get();
  if (sensor != nullptr) {
    sensor->set_quality(sensor, sensorQuality);
  }
  if (!gAfReady) {
    xSemaphoreGive(sMutex);
    primeFocus();
    if (xSemaphoreTake(sMutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
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
  if (frame == nullptr || frame->buf == nullptr || frame->len < 4 ||
      frame->format != PIXFORMAT_JPEG) {
    if (frame != nullptr) {
      esp_camera_fb_return(frame);
    }
    xSemaphoreGive(sMutex);
    return false;
  }

  uint8_t *copy = static_cast<uint8_t *>(ps_malloc(frame->len));
  if (copy == nullptr) {
    copy = static_cast<uint8_t *>(malloc(frame->len));
  }
  if (copy == nullptr) {
    esp_camera_fb_return(frame);
    xSemaphoreGive(sMutex);
    return false;
  }
  memcpy(copy, frame->buf, frame->len);
  out.data = copy;
  out.length = frame->len;
  out.width = static_cast<int>(frame->width);
  out.height = static_cast<int>(frame->height);
  out.encodeMs = 0;
  esp_camera_fb_return(frame);
  xSemaphoreGive(sMutex);

  out.crc32 = esp_rom_crc32_le(0, out.data, out.length);
  (void)sFrameCallback;
  return true;
}

void Camera::release(JpegBuffer &buffer) {
  free(buffer.data);
  buffer = JpegBuffer{};
}

}  // namespace Somnium
