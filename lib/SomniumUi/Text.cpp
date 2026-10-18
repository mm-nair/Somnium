#include "Text.h"

#include <string.h>

namespace Somnium {
namespace Ui {
namespace {

class BoundedTextCanvas : public Adafruit_GFX {
 public:
  BoundedTextCanvas(Canvas &canvas, Rect box)
      : Adafruit_GFX(box.w, box.h), canvas_(canvas), box_(box) {}

  void drawPixel(int16_t x, int16_t y, uint16_t color) override {
    if (x >= 0 && y >= 0 && x < box_.w && y < box_.h) {
      canvas_.fillRect({static_cast<int16_t>(box_.x + x),
                        static_cast<int16_t>(box_.y + y), 1, 1},
                       Color(color));
    }
  }

 private:
  Canvas &canvas_;
  Rect box_;
};

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
  if (maxWidth < glyphW(size)) {
    return {0, 0};
  }
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
  if (text == nullptr || *text == '\0' || box.empty()) {
    return;
  }
  const int16_t columns = box.w / glyphW(style.size);
  const int16_t availableRows = box.h / glyphH(style.size);
  if (columns == 0 || availableRows == 0) {
    return;
  }
  int16_t rows = 1;
  for (const char *p = text; *p != '\0' && rows < availableRows; ++p) {
    if (*p == '\n') {
      ++rows;
    }
  }
  int16_t y = 0;
  if (style.valign == VAlign::Middle) {
    y = static_cast<int16_t>((box.h - rows * glyphH(style.size)) / 2);
  } else if (style.valign == VAlign::Bottom) {
    y = static_cast<int16_t>(box.h - rows * glyphH(style.size));
  }
  BoundedTextCanvas textCanvas(canvas, box);
  textCanvas.setTextSize(style.size);
  textCanvas.setTextColor(style.color);
  textCanvas.setTextWrap(false);
  const char *line = text;
  for (int16_t row = 0; row < rows; ++row) {
    const char *end = strchr(line, '\n');
    if (end == nullptr) {
      end = line + strlen(line);
    }
    const size_t length = static_cast<size_t>(end - line);
    const bool truncated = length > static_cast<size_t>(columns) ||
                           (row == rows - 1 && *end != '\0');
    const int16_t dots = truncated ? (columns < 3 ? columns : 3) : 0;
    const size_t prefix = length < static_cast<size_t>(columns - dots)
                              ? length
                              : static_cast<size_t>(columns - dots);
    const int16_t width =
        static_cast<int16_t>((prefix + dots) * glyphW(style.size));
    int16_t x = 0;
    if (style.align == Align::Center) {
      x = static_cast<int16_t>((box.w - width) / 2);
    } else if (style.align == Align::Right) {
      x = static_cast<int16_t>(box.w - width);
    }
    textCanvas.setCursor(x, y);
    for (size_t character = 0; character < prefix; ++character) {
      textCanvas.write(static_cast<uint8_t>(line[character]));
    }
    for (int16_t dot = 0; dot < dots; ++dot) {
      textCanvas.write('.');
    }
    y = static_cast<int16_t>(y + glyphH(style.size));
    line = *end == '\0' ? end : end + 1;
  }
}

void Text::drawFontIn(Canvas &canvas, const char *text, Rect box,
                      const GFXfont &font, Color color) {
  if (text == nullptr || box.empty()) {
    return;
  }
  int32_t left = 0;
  int32_t right = 0;
  int32_t top = 0;
  int32_t bottom = 0;
  int32_t cursor = 0;
  bool hasPixels = false;
  for (const char *p = text; *p != '\0' && *p != '\n'; ++p) {
    const uint8_t character = static_cast<uint8_t>(*p);
    if (character < font.first || character > font.last) {
      continue;
    }
    const GFXglyph &glyph = font.glyph[character - font.first];
    if (glyph.width != 0 && glyph.height != 0) {
      const int32_t glyphLeft = cursor + glyph.xOffset;
      const int32_t glyphRight = glyphLeft + glyph.width;
      const int32_t glyphTop = glyph.yOffset;
      const int32_t glyphBottom = glyphTop + glyph.height;
      if (!hasPixels || glyphLeft < left) {
        left = glyphLeft;
      }
      if (!hasPixels || glyphRight > right) {
        right = glyphRight;
      }
      if (!hasPixels || glyphTop < top) {
        top = glyphTop;
      }
      if (!hasPixels || glyphBottom > bottom) {
        bottom = glyphBottom;
      }
      hasPixels = true;
    }
    cursor += glyph.xAdvance;
  }
  if (!hasPixels) {
    return;
  }
  const int32_t width = right - left;
  const int32_t height = bottom - top;
  int32_t fittedWidth = width;
  int32_t fittedHeight = height;
  if (fittedWidth > box.w) {
    fittedWidth = box.w;
    fittedHeight = height * fittedWidth / width;
  }
  if (fittedHeight > box.h) {
    fittedHeight = box.h;
    fittedWidth = width * fittedHeight / height;
  }
  if (fittedWidth == 0) {
    fittedWidth = 1;
  }
  if (fittedHeight == 0) {
    fittedHeight = 1;
  }
  const int16_t x = static_cast<int16_t>(box.x + (box.w - fittedWidth) / 2);
  const int16_t y = static_cast<int16_t>(box.y + (box.h - fittedHeight) / 2);
  cursor = 0;
  for (const char *p = text; *p != '\0' && *p != '\n'; ++p) {
    const uint8_t character = static_cast<uint8_t>(*p);
    if (character < font.first || character > font.last) {
      continue;
    }
    const GFXglyph &glyph = font.glyph[character - font.first];
    for (int16_t row = 0; row < glyph.height; ++row) {
      for (int16_t column = 0; column < glyph.width; ++column) {
        const uint16_t bit = row * glyph.width + column;
        const uint8_t bits =
            pgm_read_byte(font.bitmap + glyph.bitmapOffset + bit / 8);
        if ((bits & (0x80 >> (bit & 7))) == 0) {
          continue;
        }
        const int16_t pixelX = static_cast<int16_t>(
            x + (cursor + glyph.xOffset + column - left) * fittedWidth / width);
        const int16_t pixelY = static_cast<int16_t>(
            y + (glyph.yOffset + row - top) * fittedHeight / height);
        canvas.fillRect({pixelX, pixelY, 1, 1}, color);
      }
    }
    cursor += glyph.xAdvance;
  }
}

void Text::drawWrapped(Canvas &canvas, const char *text, Rect box,
                       const TextStyle &style) {
  if (text == nullptr || box.w < glyphW(style.size) ||
      box.h < glyphH(style.size)) {
    return;
  }
  Size block = measureWrapped(text, style.size, box.w);
  if (block.h > box.h) {
    block.h =
        static_cast<int16_t>(box.h / glyphH(style.size) * glyphH(style.size));
  }
  int16_t y = box.y;
  if (style.valign == VAlign::Middle) {
    y = static_cast<int16_t>(box.y + (box.h - block.h) / 2);
  } else if (style.valign == VAlign::Bottom) {
    y = static_cast<int16_t>(box.bottom() - block.h);
  }

  const char *p = text;
  char lineBuf[64];
  while (*p != '\0' && y + glyphH(style.size) <= box.bottom()) {
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
