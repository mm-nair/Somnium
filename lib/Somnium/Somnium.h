#pragma once

#include "Camera.h"
#include "Config.h"
#include "Console.h"
#include "Display.h"
#include "Host.h"
#include "Image.h"
#include "Inputs.h"
#include "Wifi.h"

namespace Somnium {

inline bool begin() {
  Console::begin(Defaults::kSerialBaud);
  Inputs::begin();
  Display::begin();
  const bool cameraOk = Camera::begin(Defaults::kCameraWidth);
  Wifi::begin();
  Console::println("READY");
  return cameraOk;
}

}  // namespace Somnium
