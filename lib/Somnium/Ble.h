#pragma once

#include <stddef.h>
#include <stdint.h>

#include "BleProto.h"

#ifndef SOMNIUM_BLE
#define SOMNIUM_BLE 0
#endif

namespace Somnium {

// Phone GATT server. Commands are posted into Inputs; the UI loop is the
// only place that changes screens or the selected style. The USB cook path
// is not used here and this class never writes Serial.
class Ble {
 public:
  struct ModeEntry {
    char id[BleProto::kNameBytes];
    char label[BleProto::kNameBytes];
  };

  // Process screen: reject shutter writes. Cooking (not the error hold):
  // also reject style writes, so a tap cannot retarget the in-flight cook.
  static constexpr uint8_t kGateProcess = 0x01;
  static constexpr uint8_t kGateCooking = 0x02;

  static bool compiled();
  static bool begin();
  static bool active();
  static void poll();
  static void setGate(uint8_t gate);
  static void setModes(const ModeEntry *modes, uint8_t count);
  static void setStatus(const BleProto::Status &status);
  static void offerDream(const uint8_t *jpeg, size_t jpegLen, uint8_t modeIndex);
  static void noteDreamFailed(uint8_t modeIndex);
};

}  // namespace Somnium
