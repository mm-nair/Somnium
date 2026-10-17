#pragma once

#include "Types.h"

namespace Somnium {
namespace Ui {

struct Theme {
  Color bg = Color::rgb565(12, 14, 18);
  Color surface = Color::rgb565(28, 32, 40);
  Color surfaceAlt = Color::rgb565(40, 46, 56);
  Color text = Color::rgb565(236, 240, 244);
  Color textMuted = Color::rgb565(140, 148, 160);
  Color accent = Color::rgb565(90, 180, 170);
  Color danger = Color::rgb565(220, 80, 80);
  Color overlay = Color::rgb565(0, 0, 0);

  uint8_t spaceXs = 4;
  uint8_t spaceSm = 8;
  uint8_t spaceMd = 16;
  uint8_t spaceLg = 24;

  uint8_t textSm = 1;
  uint8_t textMd = 2;
  uint8_t textLg = 3;

  uint16_t transitionMs = 220;
};

}  // namespace Ui
}  // namespace Somnium
