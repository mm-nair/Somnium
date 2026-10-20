#pragma once

#include "Ble.h"
#include "Camera.h"
#include "Config.h"
#include "Console.h"
#include "Display.h"
#include "Host.h"
#include "Image.h"
#include "Inputs.h"
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
  const bool cameraOk = Camera::begin(Defaults::kCameraWidth);
  Console::println(cameraOk ? "camera ok" : "camera fail");
  // After the camera so a BLE init failure still leaves USB cook alive.
  // BLE never writes this UART; the cook frames below stay exclusive.
  Ble::begin();
  Console::println(Ble::active() ? "ble ok"
                                 : (Ble::compiled() ? "ble fail" : "ble off"));
  Console::println("READY usb-cook");
  return cameraOk;
}

}  // namespace Somnium
