#pragma once

#include "Ble.h"
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
#include "Transfer.h"

namespace Somnium {

inline bool begin() {
  Console::begin(Defaults::kSerialBaud);
  Console::println("boot");
  Inputs::begin();
  Display::begin();
  Console::println("display");
  Storage::begin();
  Console::println("storage");
  const bool cameraOk = Camera::begin(Defaults::kCameraWidth);
  if (cameraOk) {
    Camera::primeFocus();
  }
  Console::println(cameraOk ? "camera ok" : "camera fail");
  Power::begin();
  Ble::begin();
  Transfer::begin();
  linkAnchor();
  httpAnchor();
  linkBegin();
  Console::println("READY phone-relay");
  return cameraOk;
}

}  // namespace Somnium
