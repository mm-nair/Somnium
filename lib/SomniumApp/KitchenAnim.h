#pragma once

#include <Canvas.h>

namespace Somnium {
namespace App {

// Shared kitchen doodles — same trio on Boot and Process (L→R):
// fire (oven), salt shaker, whisk.
struct KitchenAnim {
  static void drawFire(Somnium::Ui::Canvas &c, int16_t cx, int16_t cy,
                       uint32_t tMs, Somnium::Ui::Color pink);
  static void drawSalt(Somnium::Ui::Canvas &c, int16_t cx, int16_t cy,
                       uint32_t tMs, Somnium::Ui::Color pink);
  static void drawWhisk(Somnium::Ui::Canvas &c, int16_t cx, int16_t cy,
                        uint32_t tMs, Somnium::Ui::Color pink);
  static void drawProgressPill(Somnium::Ui::Canvas &c, Somnium::Ui::Rect box,
                               float progress, Somnium::Ui::Color pink,
                               Somnium::Ui::Color dim);
};

}  // namespace App
}  // namespace Somnium
