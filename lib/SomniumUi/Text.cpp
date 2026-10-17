#include "Text.h"

#include <string.h>

namespace Somnium {
namespace Ui {
namespace {

int16_t lineWidth(const char *start, const char *end, uint8_t size) {
  return static_cast<int16_t>((end - start) * Text::glyphW(size));
}

const char *nextLineEnd(const char *text, int16_t maxWidth, uint8_t size) {
  if (text == nullptr || *text == '\0') {
    return text;
  }
  const int16_t maxChars =
      maxWidth <= 0 ? 0 : static_cast<int16_t>(maxWidth / Text::glyphW(size));
  if (maxChars <= 0) {
    return text;
  }
  const char *p = text;
  const char *lastSpace = nullptr;
  int16_t count = 0;
  while (*p != '\0' && *p != '\n' && count < maxChars) {
    if (*p == ' ') {
      lastSpace = p;
    }
    ++p;
    ++count;
  }
  if (*p == '\0' || *p == '\n') {
    return p;
  }
  if (lastSpace != nullptr && lastSpace > text) {
    return lastSpace;
  }
  return p;
}

}  // namespace

Size Text::measure(const char *text, uint8_t size) {
  if (text == nullptr || *text == '\0') {
    return {0, glyphH(size)};
  }
  int16_t maxW = 0;
  int16_t lines = 1;
  int16_t col = 0;
  for (const char *p = text; *p != '\0'; ++p) {
    if (*p == '\n') {
      if (col > maxW) {
        maxW = col;
      }
      col = 0;
      ++lines;
      continue;
    }
    ++col;
  }
  if (col > maxW) {
    maxW = col;
  }
  return {static_cast<int16_t>(maxW * glyphW(size)),
          static_cast<int16_t>(lines * glyphH(size))};
}

Size Text::measureWrapped(const char *text, uint8_t size, int16_t maxWidth) {
  if (text == nullptr || *text == '\0') {
    return {0, glyphH(size)};
  }
  int16_t lines = 0;
  int16_t maxW = 0;
  const char *p = text;
  while (*p != '\0') {
    if (*p == '\n') {
      ++lines;
      ++p;
      continue;
    }
    const char *end = nextLineEnd(p, maxWidth, size);
    const int16_t w = lineWidth(p, end, size);
    if (w > maxW) {
      maxW = w;
    }
    ++lines;
    p = end;
    if (*p == '\n' || *p == ' ') {
      ++p;
    }
  }
  if (lines == 0) {
    lines = 1;
  }
  return {maxW, static_cast<int16_t>(lines * glyphH(size))};
}

void Text::draw(Canvas &canvas, const char *text, int16_t x, int16_t y,
                const TextStyle &style) {
  Adafruit_GFX *gfx = canvas.gfx();
  if (gfx == nullptr || text == nullptr) {
    return;
  }
  const Point origin = canvas.origin();
  gfx->setTextSize(style.size);
  gfx->setTextColor(style.color);
  gfx->setTextWrap(false);
  gfx->setCursor(x + origin.x, y + origin.y);
  gfx->print(text);
}

void Text::drawIn(Canvas &canvas, const char *text, Rect box,
                  const TextStyle &style) {
  if (text == nullptr || box.empty()) {
    return;
  }
  const Size size = measure(text, style.size);
  int16_t x = box.x;
  int16_t y = box.y;
  if (style.align == Align::Center) {
    x = static_cast<int16_t>(box.x + (box.w - size.w) / 2);
  } else if (style.align == Align::Right) {
    x = static_cast<int16_t>(box.right() - size.w);
  }
  if (style.valign == VAlign::Middle) {
    y = static_cast<int16_t>(box.y + (box.h - size.h) / 2);
  } else if (style.valign == VAlign::Bottom) {
    y = static_cast<int16_t>(box.bottom() - size.h);
  }
  draw(canvas, text, x, y, style);
}

void Text::drawWrapped(Canvas &canvas, const char *text, Rect box,
                       const TextStyle &style) {
  if (text == nullptr || box.empty()) {
    return;
  }
  const Size block = measureWrapped(text, style.size, box.w);
  int16_t y = box.y;
  if (style.valign == VAlign::Middle) {
    y = static_cast<int16_t>(box.y + (box.h - block.h) / 2);
  } else if (style.valign == VAlign::Bottom) {
    y = static_cast<int16_t>(box.bottom() - block.h);
  }

  const char *p = text;
  char lineBuf[64];
  while (*p != '\0' && y < box.bottom()) {
    if (*p == '\n') {
      y = static_cast<int16_t>(y + glyphH(style.size));
      ++p;
      continue;
    }
    const char *end = nextLineEnd(p, box.w, style.size);
    const size_t len = static_cast<size_t>(end - p);
    const size_t copy = len < sizeof(lineBuf) - 1 ? len : sizeof(lineBuf) - 1;
    memcpy(lineBuf, p, copy);
    lineBuf[copy] = '\0';

    TextStyle lineStyle = style;
    lineStyle.valign = VAlign::Top;
    drawIn(canvas, lineBuf,
           {box.x, y, box.w, glyphH(style.size)}, lineStyle);

    y = static_cast<int16_t>(y + glyphH(style.size));
    p = end;
    if (*p == '\n' || *p == ' ') {
      ++p;
    }
  }
}

}  // namespace Ui
}  // namespace Somnium
