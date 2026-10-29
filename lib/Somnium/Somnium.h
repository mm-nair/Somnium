#pragma once

#include "Camera.h"
#include "Config.h"
#include "Console.h"
#include "Display.h"
#include "Host.h"
#include "Image.h"
#include "Inputs.h"
#include "Link.h"
#include "Power.h"
#include "Storage.h"

namespace Somnium {

inline bool begin() {
  Console::begin(Defaults::kSerialBaud);
  // Drop deep-sleep pin holds before the shutter or the panel is touched.
  Power::begin();
  Console::println("boot");
  Inputs::begin();
  Display::begin(!Power::wokeFromSleep());
  Console::println("display");
  Storage::begin();
  Console::println("storage");
  const bool cameraOk = Camera::begin(Defaults::kCameraWidth);
  Console::println(cameraOk ? "camera ok" : "camera fail");
  linkAnchor();
  httpAnchor();
  linkBegin();
  Console::println("READY usb-cook");
  // Boot time is not user idle. The 45s window starts once the device is ready.
  Power::noteActivity();
  return cameraOk;
}

}  // namespace Somnium
