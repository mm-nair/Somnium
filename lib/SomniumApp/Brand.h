#pragma once

#include <Canvas.h>
#include <Display.h>
#include <Text.h>
#include <Ui.h>

#include "CreativeGen.h"
#include "LogoBitmap.h"

namespace Somnium {
namespace App {

struct Brand {
  static void applyTheme(Somnium::Ui::Theme &theme) {
    theme.bg = Somnium::Ui::Color(Creative::kColorBg);
    theme.surface = Somnium::Ui::Color(Creative::kColorSurface);
    theme.surfaceAlt = Somnium::Ui::Color(Creative::kColorPanel);
    theme.text = Somnium::Ui::Color(Creative::kColorPink);
    theme.textMuted = Somnium::Ui::Color(Creative::kColorMuted);
    theme.accent = theme.text;
    theme.pinkDim = Somnium::Ui::Color(Creative::kColorPinkDim);
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
    gfx->setFont(&SOMNIUM_TITLE_FONT);
    gfx->setTextColor(color);
    gfx->setTextSize(1);
    gfx->setCursor(x + o.x, y + o.y);
    gfx->print(text);
    gfx->setFont(nullptr);
  }

  static void drawHeader(Somnium::Ui::Canvas &c, const Somnium::Ui::Theme &theme,
                         const char *left, bool linked, const char *dateTime) {
    c.fillRect({0, 0, c.width(), 22}, theme.surface);
    Somnium::Ui::TextStyle leftStyle(theme.text, 1, Somnium::Ui::Align::Left);
    Somnium::Ui::Text::draw(c, left, 6, 7, leftStyle);
    Somnium::Ui::TextStyle muted(theme.textMuted, 1);
    Somnium::Ui::Text::draw(c, linked ? "USB*" : "USB.",
                            static_cast<int16_t>(c.width() / 2 - 16), 7, muted);
    const int16_t tw = Somnium::Ui::Text::measure(dateTime, 1).w;
    Somnium::Ui::TextStyle right(theme.text, 1);
    Somnium::Ui::Text::draw(c, dateTime,
                            static_cast<int16_t>(c.width() - tw - 6), 7, right);
  }
};

}  // namespace App
}  // namespace Somnium
