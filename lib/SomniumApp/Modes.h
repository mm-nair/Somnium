#pragma once

#include <Arduino.h>

namespace Somnium {
namespace App {

struct Mode {
  const char *id;
  const char *label;
  uint16_t placeholderColor;  // RGB565 tint for replaceable art slot
};

// Keep in sync with creative/creative.json (id + label).
constexpr Mode kModes[] = {
    {"noir", "NOIR", 0x8430},
    {"cartoon", "CARTOON", 0xF81F},
    {"cyberpunk", "CYBER", 0x07FF},
    {"watercolor", "WASH", 0xFD20},
    {"gourmet", "GOURMET", 0xFFE0},
};
constexpr int kModeCount = sizeof(kModes) / sizeof(kModes[0]);

inline const Mode &modeAt(int index) {
  int i = index % kModeCount;
  if (i < 0) {
    i += kModeCount;
  }
  return kModes[i];
}

}  // namespace App
}  // namespace Somnium
