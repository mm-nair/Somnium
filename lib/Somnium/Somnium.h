#pragma once

#include "ApiClient.h"
#include "Camera.h"
#include "Config.h"
#include "Console.h"
#include "Display.h"
#include "Host.h"
#include "Image.h"
#include "Inputs.h"
#include "ServerConfig.h"
#include "Storage.h"
#include "TimeSync.h"
#include "Wifi.h"

namespace Somnium {

inline bool begin() {
  Console::begin(Defaults::kSerialBaud);
  Console::println("boot");
  Inputs::begin();
  // Wi‑Fi BEFORE camera: Freenove S3 CAM XCLK kills RF if camera inits first.
  if (Wifi::begin()) {
    TimeSync::begin();
  }
  Console::println("inputs+wifi");
  Display::begin();
  Console::println("display");
  Storage::begin();
  Console::println("storage");
  const bool cameraOk = Camera::begin(Defaults::kCameraWidth);
  Console::println(cameraOk ? "camera ok" : "camera fail");
  Console::println("READY");
  return cameraOk;
}

}  // namespace Somnium
