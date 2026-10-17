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
  Inputs::begin();
  Display::begin();
  Storage::begin();
  const bool cameraOk = Camera::begin(Defaults::kCameraWidth);
  if (Wifi::begin()) {
    TimeSync::begin();
  }
  Console::println("READY");
  return cameraOk;
}

}  // namespace Somnium
