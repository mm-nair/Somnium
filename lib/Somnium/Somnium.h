#pragma once

#include "Camera.h"
#include "Config.h"
#include "Console.h"
#include "Display.h"
#include "Host.h"
#include "Image.h"
#include "Inputs.h"
#include "Power.h"
#include "ShotQueue.h"
#include "Storage.h"

namespace Somnium {

inline bool begin() {
  Console::begin(Defaults::kSerialBaud);
  Console::println("boot");
  Inputs::begin();
  Display::begin();
  Console::println("display");
  Storage::begin();
  Console::println("storage");
  Power::begin();
  ShotQueue::begin();
  const bool cameraOk = Camera::begin(Defaults::kCameraWidth);
  Console::println(cameraOk ? "camera ok" : "camera fail");
  Console::println("READY usb-cook");
  return cameraOk;
}

}  // namespace Somnium
