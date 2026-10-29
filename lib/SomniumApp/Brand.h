#pragma once

#include <Canvas.h>
#include <Config.h>
#include <Display.h>
#include <Power.h>
#include <Text.h>
#include <Ui.h>
#include <stdio.h>

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
    gfx->setTextWrap(false);
    gfx->setCursor(x + o.x, y + o.y);
    gfx->print(text);
    gfx->setFont(nullptr);
  }

  static void drawTitleIn(Somnium::Ui::Canvas &c, const char *text,
                          Somnium::Ui::Rect box, Somnium::Ui::Color color) {
    Somnium::Ui::Text::drawFontIn(c, text, box, SOMNIUM_TITLE_FONT, color);
  }

  // Icon plus percent. "LOW" replaces the number at or below 10%.
  static void drawBattery(Somnium::Ui::Canvas &c, const Somnium::Ui::Theme &theme) {
    const int pct = Somnium::Power::batteryPercent();
    const bool low = pct >= 0 && pct <= Somnium::Defaults::kBatteryLowPercent;
    const Somnium::Ui::Color color = low ? theme.text : theme.textMuted;
    const int16_t iconW = 16;
    const int16_t iconH = 8;
    const int16_t x = static_cast<int16_t>(c.width() - iconW - 8);
    const int16_t y = 6;
    c.drawRect(Somnium::Ui::Rect(x, y, iconW, iconH), color);
    c.fillRect(Somnium::Ui::Rect(static_cast<int16_t>(x + iconW),
                                 static_cast<int16_t>(y + 2), 2, 4),
               color);
    if (pct > 0) {
      int fill = (iconW - 4) * pct / 100;
      if (fill < 1) {
        fill = 1;
      }
      c.fillRect(Somnium::Ui::Rect(static_cast<int16_t>(x + 2),
                                   static_cast<int16_t>(y + 2),
                                   static_cast<int16_t>(fill),
                                   static_cast<int16_t>(iconH - 4)),
                 color);
    }
    char label[8];
    if (pct < 0) {
      snprintf(label, sizeof(label), "--");
    } else if (low) {
      snprintf(label, sizeof(label), "LOW");
    } else {
      snprintf(label, sizeof(label), "%d%%", pct);
    }
    const int16_t lw = Somnium::Ui::Text::measure(label, 1).w;
    Somnium::Ui::Text::draw(c, label, static_cast<int16_t>(x - lw - 2), 6,
                            Somnium::Ui::TextStyle(color, 1));
  }

  static void drawHeader(Somnium::Ui::Canvas &c, const Somnium::Ui::Theme &theme,
                         const char *left, bool linked, const char *dateTime) {
    c.fillRect({0, 0, c.width(), 22}, theme.surface);
    Somnium::Ui::TextStyle leftStyle(theme.text, 1, Somnium::Ui::Align::Left);
    Somnium::Ui::Text::draw(c, left, 4, 7, leftStyle);
    Somnium::Ui::TextStyle muted(theme.textMuted, 1);
    Somnium::Ui::Text::draw(c, linked ? "BLE*" : "BLE.", 52, 7, muted);
    const int16_t reserve = 62;
    const int16_t tw = Somnium::Ui::Text::measure(dateTime, 1).w;
    const int16_t clockX = static_cast<int16_t>(c.width() - reserve - tw);
    if (clockX > 78) {
      Somnium::Ui::TextStyle right(theme.text, 1);
      Somnium::Ui::Text::draw(c, dateTime, clockX, 7, right);
    }
    drawBattery(c, theme);
  }
};

}  // namespace App
}  // namespace Somnium
