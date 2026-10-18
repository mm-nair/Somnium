#include "KitchenAnim.h"

#include <math.h>

namespace Somnium {
namespace App {
namespace {

void putDot(Somnium::Ui::Canvas &c, int16_t x, int16_t y,
            Somnium::Ui::Color color) {
  c.fillRect({x, y, 2, 2}, color);
}

}  // namespace

void KitchenAnim::drawFire(Somnium::Ui::Canvas &c, int16_t cx, int16_t cy,
                           uint32_t tMs, Somnium::Ui::Color pink) {
  // Stove burner + teardrop flame (wider base, pointed tip).
  const int16_t bob = static_cast<int16_t>(sinf(tMs / 90.0f) * 2.0f);
  // Burner ring
  c.fillRect({static_cast<int16_t>(cx - 9), static_cast<int16_t>(cy + 11), 18, 3},
             pink);
  c.hLine(static_cast<int16_t>(cx - 7), static_cast<int16_t>(cy + 9), 14, pink);
  // Flame body (pyramid of fills)
  c.fillRect({static_cast<int16_t>(cx - 6), static_cast<int16_t>(cy + 2 + bob), 12, 8},
             pink);
  c.fillRect({static_cast<int16_t>(cx - 4), static_cast<int16_t>(cy - 4 + bob), 8, 8},
             pink);
  c.fillRect({static_cast<int16_t>(cx - 2), static_cast<int16_t>(cy - 10 + bob), 4, 8},
             pink);
  putDot(c, static_cast<int16_t>(cx - 1), static_cast<int16_t>(cy - 14 + bob), pink);
  // Side licks
  putDot(c, static_cast<int16_t>(cx - 8), static_cast<int16_t>(cy + 4 - bob), pink);
  putDot(c, static_cast<int16_t>(cx + 6), static_cast<int16_t>(cy + 3 + bob), pink);
}

void KitchenAnim::drawSalt(Somnium::Ui::Canvas &c, int16_t cx, int16_t cy,
                           uint32_t tMs, Somnium::Ui::Color pink) {
  // Salt shaker: cylinder body, lid, perforations, light shake.
  const float shake = sinf(tMs / 160.0f) * 2.0f;
  const int16_t x = static_cast<int16_t>(cx + shake);
  c.fillRect({static_cast<int16_t>(x - 6), static_cast<int16_t>(cy - 2), 12, 16},
             pink);
  c.fillRect({static_cast<int16_t>(x - 7), static_cast<int16_t>(cy - 6), 14, 5},
             pink);
  // Lid knob
  c.fillRect({static_cast<int16_t>(x - 2), static_cast<int16_t>(cy - 9), 4, 3}, pink);
  // Holes
  putDot(c, static_cast<int16_t>(x - 3), static_cast<int16_t>(cy + 2),
         Somnium::Ui::Color::rgb565(0, 0, 0));
  putDot(c, static_cast<int16_t>(x + 1), static_cast<int16_t>(cy + 2),
         Somnium::Ui::Color::rgb565(0, 0, 0));
  putDot(c, static_cast<int16_t>(x - 3), static_cast<int16_t>(cy + 7),
         Somnium::Ui::Color::rgb565(0, 0, 0));
  putDot(c, static_cast<int16_t>(x + 1), static_cast<int16_t>(cy + 7),
         Somnium::Ui::Color::rgb565(0, 0, 0));
  // Falling grains
  if (static_cast<int>(tMs / 120) % 2 == 0) {
    putDot(c, static_cast<int16_t>(x - 1), static_cast<int16_t>(cy + 16), pink);
    putDot(c, static_cast<int16_t>(x + 3), static_cast<int16_t>(cy + 18), pink);
  }
}

void KitchenAnim::drawWhisk(Somnium::Ui::Canvas &c, int16_t cx, int16_t cy,
                            uint32_t tMs, Somnium::Ui::Color pink) {
  // Balloon whisk: short handle + rotating elliptical wire loops (≤12px).
  const float spin = tMs / 200.0f;
  c.fillRect({static_cast<int16_t>(cx - 1), static_cast<int16_t>(cy + 5), 3, 14},
             pink);
  putDot(c, static_cast<int16_t>(cx - 1), static_cast<int16_t>(cy + 3), pink);

  for (int loop = 0; loop < 3; ++loop) {
    const float a0 = spin + loop * 0.55f;
    const float ca = cosf(a0);
    const float sa = sinf(a0);
    for (int t = 0; t < 10; ++t) {
      const float u = t * (6.2831853f / 10.0f);
      const float lx = cosf(u) * 9.0f;
      const float ly = sinf(u) * 5.5f - 1.0f;
      const int16_t x = static_cast<int16_t>(cx + lx * ca - ly * sa * 0.35f);
      const int16_t y = static_cast<int16_t>(cy + lx * sa * 0.35f + ly * ca);
      putDot(c, x, y, pink);
    }
  }
}

void KitchenAnim::drawProgressPill(Somnium::Ui::Canvas &c, Somnium::Ui::Rect box,
                                   float progress, Somnium::Ui::Color pink,
                                   Somnium::Ui::Color dim) {
  if (box.empty()) {
    return;
  }
  if (progress < 0) {
    progress = 0;
  }
  if (progress > 1) {
    progress = 1;
  }
  c.fillRect(box, dim);
  const int16_t fill = static_cast<int16_t>(box.w * progress);
  if (fill > 0) {
    c.fillRect({box.x, box.y, fill, box.h}, pink);
  }
  c.drawRect(box, pink);
}

}  // namespace App
}  // namespace Somnium
