#include "Pages.h"

#include <Config.h>
#include <Console.h>
#include <Image.h>
#include <Ui.h>
#include <esp_heap_caps.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Brand.h"
#include "Chrono.h"
#include "KitchenAnim.h"

namespace Somnium {
namespace App {
namespace {

using Somnium::Ui::Align;
using Somnium::Ui::Color;
using Somnium::Ui::Rect;
using Somnium::Ui::Text;
using Somnium::Ui::TextStyle;

constexpr uint32_t kBootMs = 9000;
// Idle return from Review. Flipping raw/dream restarts this window.
constexpr uint32_t kReviewMs = 8000;

Somnium::Ui::Theme &T() { return Somnium::Ui::Ui::theme(); }

TextStyle mutedCenter() {
  return TextStyle(T().textMuted, 1, Align::Center);
}

const char *bootStage(uint32_t ms) {
  if (ms < 1800) {
    return "warming the oven...";
  }
  if (ms < 3600) {
    return "whisking starlight...";
  }
  if (ms < 5400) {
    return "salting the sensors...";
  }
  if (ms < 7200) {
    return "proofing the pixels...";
  }
  return "pick a look...";
}

void blitPreview(Somnium::Ui::Canvas &c, const uint16_t *preview, int16_t x,
                 int16_t y, int16_t w, int16_t h) {
  if (preview == nullptr || w <= 0 || h <= 0 || w > 320) {
    return;
  }
  uint16_t row[320];
  for (int16_t rowY = 0; rowY < h; ++rowY) {
    const int16_t sourceY =
        static_cast<int16_t>(rowY * kModePreviewHeight / h);
    for (int16_t col = 0; col < w; ++col) {
      const int16_t sourceX = static_cast<int16_t>(col * kModePreviewWidth / w);
      row[col] = pgm_read_word(
          &preview[static_cast<int32_t>(sourceY) * kModePreviewWidth + sourceX]);
    }
    c.blitRgb565(x, static_cast<int16_t>(y + rowY), w, 1, row);
  }
}

}  // namespace

void BootPage::onEnter() {
  elapsed_ = 0;
  done_ = false;
  markDirty();
}

void BootPage::update(uint32_t dtMs) {
  elapsed_ += dtMs;
  markDirty();
  if (elapsed_ >= kBootMs) {
    done_ = true;
  }
}

void BootPage::draw(Somnium::Ui::Canvas &c) {
  c.clear(T().bg);
  Brand::drawLogo(c, static_cast<int16_t>((c.width() - kLogoW) / 2), 28);

  KitchenAnim::drawFire(c, 72, 110, elapsed_, T().pinkDim);
  KitchenAnim::drawSalt(c, 160, 110, elapsed_, T().text);
  KitchenAnim::drawWhisk(c, 248, 110, elapsed_ + 200, T().pinkDim);

  const float p = static_cast<float>(elapsed_) / static_cast<float>(kBootMs);
  Text::drawIn(c, bootStage(elapsed_), Rect(0, 168, c.width(), 16),
               mutedCenter());
  KitchenAnim::drawProgressPill(c, Rect(40, 198, 240, 14), p, T().text,
                                T().surface);
}

void ModePage::onEnter() {
  slide_ = 0;
  pendingNudge_ = 0;
  takePhoto_ = false;
  openMenu_ = false;
  headerOnly_ = false;
  slideTween_ = Somnium::Ui::Tween();
  clockMs_ = 0;
  markDirty();
}

void ModePage::nudge(int delta) {
  if (delta == 0) {
    return;
  }
  if (slideTween_.active) {
    pendingNudge_ += delta;
    return;
  }
  const int direction = delta > 0 ? 1 : -1;
  pendingNudge_ += delta - direction;
  index_ = (index_ + direction + kModeCount) % kModeCount;
  slide_ = static_cast<float>(direction);
  slideTween_.start(slide_, 0.0f, 260, millis(),
                    Somnium::Ui::Easing::easeInOutQuad);
  headerOnly_ = false;
  markDirty();
}

void ModePage::update(uint32_t dtMs) {
  clockMs_ += dtMs;
  if (slideTween_.active) {
    slideTween_.update(millis());
    slide_ = slideTween_.value;
    headerOnly_ = false;
    markDirty();
    if (!slideTween_.active && pendingNudge_ != 0) {
      const int next = pendingNudge_ > 0 ? 1 : -1;
      pendingNudge_ -= next;
      nudge(next);
    }
  } else {
    slide_ = 0;
    if (pendingNudge_ != 0) {
      const int next = pendingNudge_ > 0 ? 1 : -1;
      pendingNudge_ -= next;
      nudge(next);
    } else if (clockMs_ >= 1000) {
      clockMs_ = 0;
      headerOnly_ = true;
      markDirty();
    }
  }
}

bool ModePage::handleAction(Somnium::Ui::NavAction action) {
  switch (action) {
    case Somnium::Ui::NavAction::Left:
      nudge(-1);
      return true;
    case Somnium::Ui::NavAction::Right:
      nudge(1);
      return true;
    case Somnium::Ui::NavAction::Select:
      takePhoto_ = true;
      return true;
    case Somnium::Ui::NavAction::Back:
      openMenu_ = true;
      return true;
    default:
      return false;
  }
}

void ModePage::drawHeaderOnly(Somnium::Ui::Canvas &c) {
  char when[32];
  Chrono::formatDateTime(when, sizeof(when));
  Brand::drawHeader(c, T(), "Looks", true, when);
}

void ModePage::drawHero(Somnium::Ui::Canvas &c, int modeIndex, int16_t xOffset,
                        bool titled) {
  const Mode &mode = modeAt(modeIndex);
  if (titled) {
    c.fillRect(Rect(16, 30, 288, 118), T().surfaceAlt);
    c.drawRect(Rect(16, 30, 288, 118), T().text);
  }
  constexpr int16_t imgW = 168;
  constexpr int16_t imgH = 100;
  const int16_t imgX = static_cast<int16_t>((c.width() - imgW) / 2 + xOffset);
  const int16_t imgY = 36;
  if (mode.preview != nullptr) {
    blitPreview(c, mode.preview, imgX, imgY, imgW, imgH);
  } else {
    c.fillRect(Rect(imgX, imgY, imgW, imgH), Color(mode.placeholderColor));
    Text::drawIn(c, mode.label,
                 Rect(imgX, static_cast<int16_t>(imgY + 42), imgW, 14),
                 TextStyle(T().bg, 1, Align::Center));
  }
  if (titled) {
    Brand::drawTitleIn(c, mode.label, Rect(24, 152, 272, 24), T().text);
  }
}

void ModePage::drawFilmstrip(Somnium::Ui::Canvas &c) {
  constexpr int16_t thumbW = 44;
  constexpr int16_t thumbH = 26;
  constexpr int16_t gap = 8;
  const int16_t total =
      static_cast<int16_t>(kModeCount * thumbW + (kModeCount - 1) * gap);
  int16_t x = static_cast<int16_t>((c.width() - total) / 2);
  const int16_t y = 182;
  for (int i = 0; i < kModeCount; ++i) {
    const Mode &mode = modeAt(i);
    const bool selected = i == index_;
    c.drawRect(Rect(x, y, thumbW, thumbH), selected ? T().text : T().pinkDim);
    if (mode.preview != nullptr) {
      blitPreview(c, mode.preview, static_cast<int16_t>(x + 2),
                  static_cast<int16_t>(y + 2), static_cast<int16_t>(thumbW - 4),
                  static_cast<int16_t>(thumbH - 4));
    } else {
      c.fillRect(Rect(static_cast<int16_t>(x + 2), static_cast<int16_t>(y + 2),
                      static_cast<int16_t>(thumbW - 4),
                      static_cast<int16_t>(thumbH - 4)),
                 Color(mode.placeholderColor));
    }
    x = static_cast<int16_t>(x + thumbW + gap);
  }
}

void ModePage::drawLookbook(Somnium::Ui::Canvas &c) {
  if (slide_ == 0.0f) {
    drawHero(c, index_, 0, true);
  } else {
    const int direction = slide_ > 0.0f ? 1 : -1;
    const int outgoing = (index_ - direction + kModeCount) % kModeCount;
    const int16_t incomingX = static_cast<int16_t>(slide_ * 36.0f);
    const int16_t outgoingX =
        static_cast<int16_t>((slide_ - static_cast<float>(direction)) * 36.0f);
    const bool incomingFront = slide_ <= 0.5f && slide_ >= -0.5f;
    if (incomingFront) {
      drawHero(c, outgoing, outgoingX, false);
      drawHero(c, index_, incomingX, true);
    } else {
      drawHero(c, index_, incomingX, false);
      drawHero(c, outgoing, outgoingX, true);
    }
  }
  drawFilmstrip(c);
  Text::drawIn(c, "turn: look   click: menu   shutter: shoot",
               Rect(0, 216, c.width(), 14), mutedCenter());
}

void ModePage::draw(Somnium::Ui::Canvas &c) {
  if (headerOnly_) {
    drawHeaderOnly(c);
    headerOnly_ = false;
    return;
  }
  c.clear(T().bg);
  drawHeaderOnly(c);
  drawLookbook(c);
}

void MenuPage::onEnter() {
  index_ = 0;
  clockMs_ = 0;
  markDirty();
}

void MenuPage::setContext(const char *lookLabel, int plates) {
  strncpy(look_, lookLabel != nullptr ? lookLabel : "", sizeof(look_) - 1);
  look_[sizeof(look_) - 1] = '\0';
  plates_ = plates < 0 ? 0 : plates;
  markDirty();
}

void MenuPage::nudge(int delta) {
  if (delta == 0) {
    return;
  }
  const int step = delta > 0 ? 1 : -1;
  index_ = (index_ + step + kCount) % kCount;
  markDirty();
}

void MenuPage::update(uint32_t dtMs) {
  clockMs_ += dtMs;
  if (clockMs_ >= 1000) {
    clockMs_ = 0;
    markDirty();
  }
}

void MenuPage::draw(Somnium::Ui::Canvas &c) {
  c.clear(T().bg);
  char when[32];
  Chrono::formatDateTime(when, sizeof(when));
  Brand::drawHeader(c, T(), "Menu", true, when);

  char summary[40];
  snprintf(summary, sizeof(summary), plates_ == 1 ? "1 plate" : "%d plates",
           plates_);
  Text::drawIn(c, summary, Rect(0, 36, c.width(), 14), mutedCenter());

  char shoot[40];
  snprintf(shoot, sizeof(shoot), "Shoot  %s", look_[0] != '\0' ? look_ : "look");
  const char *rows[] = {"Gallery", shoot, "Back"};
  for (int i = 0; i < kCount; ++i) {
    const int16_t y = static_cast<int16_t>(64 + i * 40);
    const bool on = i == index_;
    c.fillRect(Rect(24, y, 272, 32), on ? T().surfaceAlt : T().bg);
    if (on) {
      c.fillRect(Rect(24, y, 4, 32), T().text);
      c.drawRect(Rect(24, y, 272, 32), T().text);
    }
    Text::drawIn(c, rows[i], Rect(40, static_cast<int16_t>(y + 12), 240, 12),
                 TextStyle(on ? T().text : T().textMuted, 1));
  }
  Text::drawIn(c, "turn: move   click: choose   shutter: shoot",
               Rect(0, 214, c.width(), 14), mutedCenter());
}

void ProcessPage::onEnter() {
  elapsed_ = 0;
  animationMs_ = 0;
  failed_ = false;
  stage_ = CookStage::Capture;
  strncpy(status_, "capturing the frame", sizeof(status_) - 1);
  status_[sizeof(status_) - 1] = '\0';
  markDirty();
}

void ProcessPage::setStage(CookStage stage) {
  stage_ = stage;
  markDirty();
}

void ProcessPage::setStatus(const char *status) {
  strncpy(status_, status != nullptr ? status : "", sizeof(status_) - 1);
  status_[sizeof(status_) - 1] = '\0';
  markDirty();
}

void ProcessPage::update(uint32_t dtMs) {
  elapsed_ += dtMs;
  animationMs_ += dtMs;
  if (animationMs_ >= 100) {
    animationMs_ %= 100;
    markDirty();
  }
}

void ProcessPage::draw(Somnium::Ui::Canvas &c) {
  c.clear(T().bg);
  Text::drawIn(c, failed_ ? "Kitchen trouble" : "Cooking",
               Rect(0, 8, c.width(), 18), TextStyle(T().text, 2, Align::Center));
  if (label_ != nullptr && label_[0] != '\0') {
    Text::drawIn(c, label_, Rect(0, 28, c.width(), 12), mutedCenter());
  }

  static const char *names[] = {"CAPTURE", "SEND", "STYLE", "RECEIVE"};
  constexpr int16_t boxW = 70;
  constexpr int16_t gap = 6;
  constexpr int16_t boxH = 28;
  const int16_t total = static_cast<int16_t>(4 * boxW + 3 * gap);
  int16_t x = static_cast<int16_t>((c.width() - total) / 2);
  const int16_t y = 48;
  const int active = static_cast<int>(stage_);
  for (int i = 0; i < 4; ++i) {
    const bool current = i == active;
    const bool done = i < active && !failed_;
    const Color ink =
        (failed_ && current) ? T().danger : (current || done ? T().text : T().textMuted);
    c.fillRect(Rect(x, y, boxW, boxH), T().surfaceAlt);
    c.drawRect(Rect(x, y, boxW, boxH),
               (failed_ && current) ? T().danger
                                    : (current || done ? T().text : T().pinkDim));
    Text::drawIn(c, names[i], Rect(x, static_cast<int16_t>(y + 10), boxW, 10),
                 TextStyle(ink, 1, Align::Center));
    if (i < 3) {
      c.hLine(static_cast<int16_t>(x + boxW), static_cast<int16_t>(y + boxH / 2),
              gap, T().pinkDim);
    }
    x = static_cast<int16_t>(x + boxW + gap);
  }

  Text::drawIn(c, status_, Rect(8, 86, static_cast<int16_t>(c.width() - 16), 14),
               mutedCenter());
  char clock[16];
  const uint32_t seconds = elapsed_ / 1000;
  snprintf(clock, sizeof(clock), "%lu:%02lu",
           static_cast<unsigned long>(seconds / 60),
           static_cast<unsigned long>(seconds % 60));
  Text::drawIn(c, clock, Rect(0, 104, c.width(), 12), mutedCenter());

  // Same L→R kitchen trio as Boot, kept inside the 40x40 tiles.
  const int16_t iconY = 128;
  c.fillRect(Rect(36, iconY, 40, 40), T().surfaceAlt);
  c.fillRect(Rect(140, iconY, 40, 40), T().surfaceAlt);
  c.fillRect(Rect(244, iconY, 40, 40), T().surfaceAlt);
  c.hLine(76, static_cast<int16_t>(iconY + 20), 64, T().pinkDim);
  c.hLine(180, static_cast<int16_t>(iconY + 20), 64, T().pinkDim);
  KitchenAnim::drawFire(c, 56, static_cast<int16_t>(iconY + 20), elapsed_,
                        T().text);
  KitchenAnim::drawSalt(c, 160, static_cast<int16_t>(iconY + 20), elapsed_,
                        T().pinkDim);
  KitchenAnim::drawWhisk(c, 264, static_cast<int16_t>(iconY + 20), elapsed_,
                         T().text);

  Text::drawIn(c, failed_ ? "click dial to return" : "dial locked while cooking",
               Rect(0, 214, c.width(), 14), mutedCenter());
}

void PreviewPage::releaseRgb() {
  free(rgb_);
  rgb_ = nullptr;
  rgbW_ = 0;
  rgbH_ = 0;
  rgbOk_ = false;
}

const uint8_t *PreviewPage::activeJpeg(size_t *length) const {
  if (side_ == Side::Raw && alt_ != nullptr && altLen_ > 0) {
    *length = altLen_;
    return alt_;
  }
  *length = jpegLen_;
  return jpeg_;
}

bool PreviewPage::ensureRgb(int16_t w, int16_t h) {
  if (rgbOk_ && rgb_ != nullptr && rgbW_ == w && rgbH_ == h) {
    return true;
  }
  size_t length = 0;
  const uint8_t *jpeg = activeJpeg(&length);
  if (jpeg == nullptr || length == 0) {
    return false;
  }
  releaseRgb();
  const size_t bytes =
      static_cast<size_t>(w) * static_cast<size_t>(h) * sizeof(uint16_t);
  rgb_ = static_cast<uint16_t *>(
      heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (rgb_ == nullptr) {
    rgb_ = static_cast<uint16_t *>(malloc(bytes));
  }
  if (rgb_ == nullptr) {
    return false;
  }
  rgbW_ = w;
  rgbH_ = h;
  rgbOk_ = Somnium::Image::decodeRgb565(jpeg, length, rgb_, w, h);
  if (!rgbOk_) {
    releaseRgb();
  }
  return rgbOk_;
}

void PreviewPage::beginReview(const char *modeLabel, const uint8_t *jpeg,
                              size_t length) {
  kind_ = Kind::Review;
  side_ = Side::Dream;
  alt_ = nullptr;
  altLen_ = 0;
  modeLabel_ = modeLabel != nullptr ? modeLabel : "";
  index_ = 0;
  total_ = 0;
  back_ = false;
  autoDone_ = false;
  reviewStartedMs_ = 0;
  setPhoto(jpeg, length);
}

void PreviewPage::beginStorage(int index0, int total) {
  kind_ = Kind::Storage;
  side_ = Side::Dream;
  index_ = total > 0 ? index0 + 1 : 0;
  total_ = total;
  back_ = false;
  autoDone_ = false;
}

void PreviewPage::setPhoto(const uint8_t *jpeg, size_t length) {
  releaseRgb();
  jpeg_ = jpeg;
  jpegLen_ = length;
  alt_ = nullptr;
  altLen_ = 0;
  side_ = Side::Dream;
  decodePending_ = jpeg_ != nullptr && jpegLen_ > 0;
  allowDecode_ = false;
  previewPresented_ = false;
  markDirty();
}

void PreviewPage::setPair(const uint8_t *dream, size_t dreamLen,
                          const uint8_t *raw, size_t rawLen, bool showRaw) {
  releaseRgb();
  jpeg_ = dream;
  jpegLen_ = dreamLen;
  alt_ = raw;
  altLen_ = rawLen;
  const bool rawOk = raw != nullptr && rawLen > 0;
  const bool dreamOk = dream != nullptr && dreamLen > 0;
  if (showRaw && rawOk) {
    side_ = Side::Raw;
  } else if (dreamOk) {
    side_ = Side::Dream;
  } else if (rawOk) {
    side_ = Side::Raw;
  } else {
    side_ = Side::Dream;
  }
  size_t length = 0;
  const uint8_t *active = activeJpeg(&length);
  decodePending_ = active != nullptr && length > 0;
  allowDecode_ = false;
  previewPresented_ = false;
  markDirty();
}

void PreviewPage::setAlternate(const uint8_t *jpeg, size_t length) {
  alt_ = jpeg;
  altLen_ = length;
  if (side_ == Side::Raw && (alt_ == nullptr || altLen_ == 0)) {
    side_ = Side::Dream;
    releaseRgb();
    decodePending_ = jpeg_ != nullptr && jpegLen_ > 0;
    allowDecode_ = false;
  }
  markDirty();
}

void PreviewPage::setMeta(const char *modeLabel, int index1Based, int total) {
  modeLabel_ = modeLabel != nullptr ? modeLabel : "";
  index_ = index1Based;
  total_ = total;
  markDirty();
}

bool PreviewPage::flip() {
  if (alt_ == nullptr || altLen_ == 0 || jpeg_ == nullptr || jpegLen_ == 0) {
    return false;
  }
  side_ = side_ == Side::Raw ? Side::Dream : Side::Raw;
  releaseRgb();
  decodePending_ = true;
  allowDecode_ = false;
  if (kind_ == Kind::Review && previewPresented_) {
    reviewStartedMs_ = millis();
  }
  markDirty();
  return true;
}

void PreviewPage::onEnter() {
  back_ = false;
  autoDone_ = false;
  previewPresented_ = false;
  reviewStartedMs_ = 0;
  clockMs_ = 0;
  markDirty();
}

void PreviewPage::onExit() { releaseRgb(); }

void PreviewPage::update(uint32_t dtMs) {
  clockMs_ += dtMs;
  if (kind_ == Kind::Review && previewPresented_ &&
      millis() - reviewStartedMs_ >= kReviewMs) {
    autoDone_ = true;
  }
  if (clockMs_ >= 1000) {
    clockMs_ = 0;
    markDirty();
  }
  if (decodePending_ && allowDecode_ && !rgbOk_) {
    decodePending_ = false;
    size_t length = 0;
    activeJpeg(&length);
    if (!ensureRgb(Somnium::Defaults::kDisplayWidth,
                   Somnium::Defaults::kDisplayHeight)) {
      Somnium::Console::printf("JPEG decode failed %u B\n",
                               static_cast<unsigned>(length));
    }
    markDirty();
  }
}

void PreviewPage::nudge(int delta) {
  if (kind_ != Kind::Storage || total_ <= 0 || delta == 0) {
    return;
  }
  int next = index_ + (delta > 0 ? 1 : -1);
  if (next < 1) {
    next = total_;
  }
  if (next > total_) {
    next = 1;
  }
  index_ = next;
  markDirty();
}

bool PreviewPage::handleAction(Somnium::Ui::NavAction action) {
  if (action == Somnium::Ui::NavAction::Back) {
    back_ = true;
    return true;
  }
  if (kind_ == Kind::Storage) {
    if (action == Somnium::Ui::NavAction::Left) {
      nudge(-1);
      return true;
    }
    if (action == Somnium::Ui::NavAction::Right) {
      nudge(1);
      return true;
    }
  } else if (action == Somnium::Ui::NavAction::Left ||
             action == Somnium::Ui::NavAction::Right) {
    return flip();
  }
  return false;
}

void PreviewPage::drawChrome(Somnium::Ui::Canvas &c) {
  c.fillRect(Rect(0, 0, c.width(), 22), T().surface);
  const bool paired = alt_ != nullptr && altLen_ > 0 && jpeg_ != nullptr && jpegLen_ > 0;
  const char *hint = "click: back";
  if (kind_ == Kind::Review) {
    hint = paired ? "turn: flip   click: looks" : "click: looks";
  } else if (paired) {
    hint = "click: flip   hold: back";
  }
  Text::drawIn(c, hint, Rect(6, 7, 200, 10), TextStyle(T().text, 1));

  size_t activeLen = 0;
  activeJpeg(&activeLen);
  if (activeLen > 0) {
    const char *badge = side_ == Side::Raw ? "RAW" : "DREAM";
    const int16_t badgeW = static_cast<int16_t>(Text::measure(badge, 1).w + 8);
    const int16_t badgeX = static_cast<int16_t>(c.width() - badgeW - 6);
    c.fillRect(Rect(badgeX, 4, badgeW, 14), T().bg);
    Text::drawIn(c, badge, Rect(badgeX, 7, badgeW, 10),
                 TextStyle(T().text, 1, Align::Center));
  }

  c.fillRect(Rect(0, static_cast<int16_t>(c.height() - 28), c.width(), 28),
             T().surface);

  char when[32];
  Chrono::formatDateTime(when, sizeof(when));
  const int16_t clockWidth = Text::measure(when, 1).w;
  const int16_t clockX = static_cast<int16_t>(c.width() - clockWidth - 8);
  const Rect metadataBox(8, static_cast<int16_t>(c.height() - 18),
                         static_cast<int16_t>(clockX - 16), 8);
  if (kind_ == Kind::Storage) {
    char meta[48];
    if (total_ <= 0) {
      snprintf(meta, sizeof(meta), "no plates yet");
    } else {
      snprintf(meta, sizeof(meta), "%s  %d/%d", modeLabel_, index_, total_);
    }
    Text::drawIn(c, meta, metadataBox, TextStyle(T().text, 1));
  } else {
    Text::drawIn(c, modeLabel_ != nullptr ? modeLabel_ : "dream", metadataBox,
                 TextStyle(T().text, 1));
  }

  Text::drawIn(c, when,
               Rect(clockX, static_cast<int16_t>(c.height() - 18), clockWidth, 8),
               TextStyle(T().text, 1));
  c.hLine(clockX, static_cast<int16_t>(c.height() - 8), clockWidth, T().pinkDim);
}

void PreviewPage::draw(Somnium::Ui::Canvas &c) {
  c.clear(T().bg);
  if (rgbOk_ && rgb_ != nullptr) {
    c.blitRgb565(0, 0, rgbW_, rgbH_, rgb_);
  } else if (kind_ == Kind::Storage && total_ <= 0) {
    Text::drawIn(c, "no plates yet", Rect(0, 110, c.width(), 20), mutedCenter());
  } else if (decodePending_) {
    Text::drawIn(c, "plating...", Rect(0, 110, c.width(), 20), mutedCenter());
    allowDecode_ = true;
  } else {
    Text::drawIn(c, "image unavailable", Rect(0, 110, c.width(), 20),
                 mutedCenter());
  }
  if (!decodePending_ && !previewPresented_) {
    previewPresented_ = true;
    reviewStartedMs_ = millis();
  }
  drawChrome(c);
}

}  // namespace App
}  // namespace Somnium
