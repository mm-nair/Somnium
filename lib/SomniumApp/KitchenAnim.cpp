#include "KitchenAnim.h"

#include <math.h>

namespace Somnium {
namespace App {
namespace {

void drawLine(Somnium::Ui::Canvas &c, int16_t startX, int16_t startY,
               int16_t endX, int16_t endY, Somnium::Ui::Color color) {
  const int16_t deltaX = abs(endX - startX);
  const int16_t deltaY = -abs(endY - startY);
  const int16_t stepX = startX < endX ? 1 : -1;
  const int16_t stepY = startY < endY ? 1 : -1;
  int16_t error = deltaX + deltaY;
  while (true) {
    c.fillRect({startX, startY, 1, 1}, color);
    if (startX == endX && startY == endY) {
      break;
    }
    const int16_t doubledError = 2 * error;
    if (doubledError >= deltaY) {
      error += deltaY;
      startX += stepX;
    }
    if (doubledError <= deltaX) {
      error += deltaX;
      startY += stepY;
    }
  }
}

void drawEllipse(Somnium::Ui::Canvas &c, int16_t cx, int16_t cy,
                  int16_t radiusX, int16_t radiusY, Somnium::Ui::Color color) {
  int16_t previousX = cx + radiusX;
  int16_t previousY = cy;
  for (int segment = 1; segment <= 40; ++segment) {
    const float angle = segment * (6.2831853f / 40.0f);
    const int16_t x = cx + static_cast<int16_t>(lroundf(cosf(angle) * radiusX));
    const int16_t y = cy + static_cast<int16_t>(lroundf(sinf(angle) * radiusY));
    drawLine(c, previousX, previousY, x, y, color);
    previousX = x;
    previousY = y;
  }
}

}  // namespace

void KitchenAnim::drawFire(Somnium::Ui::Canvas &c, int16_t cx, int16_t cy,
                           uint32_t tMs, Somnium::Ui::Color pink) {
  const int16_t sway = static_cast<int16_t>(sinf(tMs / 600.0f) * 2.0f);
  for (int16_t offset : {-5, 5}) {
    drawLine(c, cx + offset, cy - 3, cx + offset + sway, cy - 7, pink);
    drawLine(c, cx + offset + sway, cy - 7, cx + offset - sway, cy - 11, pink);
    drawLine(c, cx + offset - sway, cy - 11, cx + offset, cy - 15, pink);
  }
  drawEllipse(c, cx, cy, 11, 2, pink);
  drawLine(c, cx - 11, cy, cx - 9, cy + 10, pink);
  drawLine(c, cx - 9, cy + 10, cx + 9, cy + 10, pink);
  drawLine(c, cx + 9, cy + 10, cx + 11, cy, pink);
  c.drawRect({static_cast<int16_t>(cx - 16), static_cast<int16_t>(cy + 1), 5, 5},
             pink);
  c.drawRect({static_cast<int16_t>(cx + 12), static_cast<int16_t>(cy + 1), 5, 5},
             pink);
  for (int16_t offset : {-6, 0, 6}) {
    const int16_t flameHeight = 2 + (tMs / 500 + (offset + 6) / 6) % 2;
    drawLine(c, cx + offset - 2, cy + 15, cx + offset, cy + 15 - flameHeight,
             pink);
    drawLine(c, cx + offset, cy + 15 - flameHeight, cx + offset + 2, cy + 15,
             pink);
  }
  c.hLine(cx - 13, cy + 17, 27, pink);
}

void KitchenAnim::drawSalt(Somnium::Ui::Canvas &c, int16_t cx, int16_t cy,
                           uint32_t tMs, Somnium::Ui::Color pink) {
  const int16_t x = cx + static_cast<int16_t>(sinf(tMs / 500.0f) * 2.0f);
  drawLine(c, x - 13, cy - 11, x - 7, cy - 17, pink);
  drawLine(c, x - 7, cy - 17, x + 3, cy - 7, pink);
  drawLine(c, x + 3, cy - 7, x - 3, cy - 1, pink);
  drawLine(c, x - 3, cy - 1, x - 13, cy - 11, pink);
  drawLine(c, x - 3, cy - 1, x - 1, cy + 1, pink);
  drawLine(c, x - 1, cy + 1, x + 5, cy - 5, pink);
  drawLine(c, x + 5, cy - 5, x + 3, cy - 7, pink);
  for (int grain = 0; grain < 3; ++grain) {
    const int16_t fall = (tMs / 160 + grain * 3) % 9;
    c.fillRect({static_cast<int16_t>(cx + 1 + grain * 3),
                static_cast<int16_t>(cy + fall), 1, 1}, pink);
  }
  c.hLine(cx - 12, cy + 10, 25, pink);
  drawLine(c, cx - 12, cy + 10, cx - 8, cy + 16, pink);
  drawLine(c, cx - 8, cy + 16, cx + 8, cy + 16, pink);
  drawLine(c, cx + 8, cy + 16, cx + 12, cy + 10, pink);
}

void KitchenAnim::drawWhisk(Somnium::Ui::Canvas &c, int16_t cx, int16_t cy,
                            uint32_t tMs, Somnium::Ui::Color pink) {
  const int16_t stir = static_cast<int16_t>(sinf(tMs / 600.0f) * 3.0f);
  drawEllipse(c, cx, cy + 6, 13, 3, pink);
  drawLine(c, cx - 13, cy + 6, cx - 10, cy + 13, pink);
  drawLine(c, cx - 10, cy + 13, cx - 6, cy + 17, pink);
  drawLine(c, cx - 6, cy + 17, cx + 6, cy + 17, pink);
  drawLine(c, cx + 6, cy + 17, cx + 10, cy + 13, pink);
  drawLine(c, cx + 10, cy + 13, cx + 13, cy + 6, pink);
  drawLine(c, cx + stir, cy - 8, cx + stir + 6, cy - 16, pink);
  drawLine(c, cx + stir + 1, cy - 8, cx + stir + 7, cy - 16, pink);
  drawEllipse(c, cx + stir, cy - 1, 5, 7, pink);
  drawEllipse(c, cx + stir, cy - 1, 2, 7, pink);
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
