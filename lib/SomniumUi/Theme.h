#pragma once

#include "Types.h"

namespace Somnium {
namespace Ui {

struct Theme {
  Color bg = Color::rgb565(0, 0, 0);
  Color surface = Color::rgb565(20, 12, 18);
  Color surfaceAlt = Color::rgb565(36, 20, 30);
  Color text = Color::rgb565(255, 105, 180);
  Color textMuted = Color::rgb565(140, 110, 125);
  Color accent = Color::rgb565(255, 105, 180);
  Color pinkDim = Color::rgb565(200, 80, 140);
  Color danger = Color::rgb565(220, 80, 80);
  Color overlay = Color::rgb565(0, 0, 0);

  uint8_t spaceXs = 4;
  uint8_t spaceSm = 8;
  uint8_t spaceMd = 16;
  uint8_t spaceLg = 24;

  uint8_t textSm = 1;
  uint8_t textMd = 2;
  uint8_t textLg = 3;

  uint16_t transitionMs = 240;
};

}  // namespace Ui
}  // namespace Somnium
