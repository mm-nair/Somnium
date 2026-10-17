#pragma once

// Umbrella header for Project Somnium hardware core.
#include "Camera.h"
#include "Config.h"
#include "Console.h"
#include "Display.h"
#include "Image.h"
#include "Inputs.h"
#include "Wifi.h"

namespace Somnium {

// Brings up serial, inputs, display, camera, and background Wi-Fi.
// Returns false if camera init fails (display/inputs may still work).
inline bool begin() {
  Console::begin(Defaults::kSerialBaud);
  Console::printBoardInfo();
  Inputs::begin();
  Display::begin();
  const bool cameraOk = Camera::begin(Defaults::kCameraWidth);
  Wifi::begin();  // non-blocking; credentials optional
  return cameraOk;
}

}  // namespace Somnium
