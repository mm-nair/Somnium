#pragma once

#include <Canvas.h>
#include <Display.h>
#include <Fonts/FreeSerifBoldItalic12pt7b.h>
#include <Text.h>
#include <Ui.h>

#include "LogoBitmap.h"

namespace Somnium {
namespace App {

struct Brand {
  static void applyTheme(Somnium::Ui::Theme &theme) {
    theme.bg = Somnium::Ui::Color::rgb565(0, 0, 0);
    theme.surface = Somnium::Ui::Color::rgb565(20, 12, 18);
    theme.surfaceAlt = Somnium::Ui::Color::rgb565(36, 20, 30);
    theme.text = Somnium::Ui::Color::rgb565(255, 105, 180);
    theme.textMuted = Somnium::Ui::Color::rgb565(140, 110, 125);
    theme.accent = theme.text;
    theme.pinkDim = Somnium::Ui::Color::rgb565(200, 80, 140);
  }

  static void drawLogo(Somnium::Ui::Canvas &c, int16_t x, int16_t y) {
    auto *gfx = c.gfx();
    if (gfx == nullptr) {
      return;
    }
    const Somnium::Ui::Point o = c.origin();
    uint16_t row[kLogoW];
    for (int16_t rowY = 0; rowY < kLogoH; ++rowY) {
      for (int16_t col = 0; col < kLogoW; ++col) {
        row[col] = pgm_read_word(&kLogoRgb565[rowY * kLogoW + col]);
      }
      gfx->drawRGBBitmap(x + o.x, y + o.y + rowY, row, kLogoW, 1);
    }
  }

  static void drawTitleFont(Somnium::Ui::Canvas &c, const char *text, int16_t x,
                            int16_t y, Somnium::Ui::Color color) {
    auto *gfx = c.gfx();
    if (gfx == nullptr || text == nullptr) {
      return;
    }
    const Somnium::Ui::Point o = c.origin();
    gfx->setFont(&FreeSerifBoldItalic12pt7b);
    gfx->setTextColor(color);
    gfx->setTextSize(1);
    gfx->setCursor(x + o.x, y + o.y);
    gfx->print(text);
    gfx->setFont(nullptr);
  }

  static void drawHeader(Somnium::Ui::Canvas &c, const Somnium::Ui::Theme &theme,
                         const char *left, bool wifiOn, const char *dateTime) {
    c.fillRect({0, 0, c.width(), 22}, theme.surface);
    Somnium::Ui::TextStyle leftStyle(theme.text, 1, Somnium::Ui::Align::Left);
    Somnium::Ui::Text::draw(c, left, 6, 7, leftStyle);
    Somnium::Ui::TextStyle muted(theme.textMuted, 1);
    Somnium::Ui::Text::draw(c, wifiOn ? "WiFi*" : "WiFi.",
                            static_cast<int16_t>(c.width() / 2 - 18), 7, muted);
    const int16_t tw = Somnium::Ui::Text::measure(dateTime, 1).w;
    Somnium::Ui::TextStyle right(theme.text, 1);
    Somnium::Ui::Text::draw(c, dateTime,
                            static_cast<int16_t>(c.width() - tw - 6), 7, right);
  }
};

}  // namespace App
}  // namespace Somnium
