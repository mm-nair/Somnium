#include "Pages.h"

#include <Config.h>
#include <Console.h>
#include <Image.h>
#include <Ui.h>
#include <esp_heap_caps.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Ble.h>

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
constexpr uint32_t kReviewMs = 3000;

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
  return "plating Somnium...";
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
  // 160x56 logo on a 240x135 panel. The kitchen trio sits under it.
  Brand::drawLogo(c, static_cast<int16_t>((c.width() - kLogoW) / 2), 0);

  const int16_t mid = static_cast<int16_t>(c.width() / 2);
  KitchenAnim::drawFire(c, static_cast<int16_t>(mid - 70), 96, elapsed_, T().pinkDim);
  KitchenAnim::drawSalt(c, mid, 96, elapsed_, T().text);
  KitchenAnim::drawWhisk(c, static_cast<int16_t>(mid + 70), 96, elapsed_ + 200,
                         T().pinkDim);

  const float p = static_cast<float>(elapsed_) / static_cast<float>(kBootMs);
  Text::drawIn(c, bootStage(elapsed_), Rect(0, 58, c.width(), 10), mutedCenter());
  KitchenAnim::drawProgressPill(
      c, Rect(20, static_cast<int16_t>(c.height() - 16), static_cast<int16_t>(c.width() - 40),
              10),
      p, T().text, T().surface);
}

void ModePage::onEnter() {
  slide_ = 0;
  pendingNudge_ = 0;
  takePhoto_ = false;
  openStorage_ = false;
  headerOnly_ = false;
  slideTween_ = Somnium::Ui::Tween();
  clockMs_ = 0;
  markDirty();
}

void ModePage::selectId(const char *id) {
  if (id == nullptr) {
    return;
  }
  for (int i = 0; i < kModeCount; ++i) {
    if (strcmp(modeAt(i).id, id) == 0) {
      index_ = i;
      slide_ = 0;
      slideTween_ = Somnium::Ui::Tween();
      markDirty();
      return;
    }
  }
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
      openStorage_ = true;
      return true;
    default:
      return false;
  }
}

void ModePage::drawHeaderOnly(Somnium::Ui::Canvas &c) {
  char when[16];
  Chrono::formatTime(when, sizeof(when));
  Brand::drawHeader(c, T(), "Somnium", Somnium::Ble::connected(), when);
}

void ModePage::drawPanel(Somnium::Ui::Canvas &c, int modeIndex, float slot,
                         float slide) {
  const float xNorm = slot + slide;
  const float depth = fabsf(xNorm);
  const float scale = 1.0f - depth * 0.18f;
  const int16_t card = static_cast<int16_t>(c.height() > 160 ? 150 : 78);
  const int16_t w = static_cast<int16_t>(card * scale);
  const int16_t h = static_cast<int16_t>(card * scale);
  const int16_t cx = static_cast<int16_t>(c.width() / 2 + xNorm * (card * 0.7f));
  const int16_t cy = static_cast<int16_t>(c.height() / 2 + 6 + depth * 4);
  const int16_t x = static_cast<int16_t>(cx - w / 2);
  const int16_t y = static_cast<int16_t>(cy - h / 2);
  if (w <= 0 || h <= 0 || x >= c.width() || x + w <= 0) {
    return;
  }

  const Mode &mode = modeAt(modeIndex);
  const float emphasis = Somnium::Ui::Easing::clamp01(1.0f - depth);
  const Color frame =
      Somnium::Ui::Easing::lerpColor(T().pinkDim, T().text, emphasis);
  c.fillRect(Rect(x, y, w, h), T().surfaceAlt);
  c.drawRect(Rect(x, y, w, h), frame);

  const int16_t imgH = static_cast<int16_t>(h * 0.55f);
  c.fillRect(Rect(static_cast<int16_t>(x + 6), static_cast<int16_t>(y + 6),
                  static_cast<int16_t>(w - 12), imgH),
             Color(mode.placeholderColor));
  if (mode.preview != nullptr && w > 12 && imgH > 0) {
    uint16_t row[kModePreviewWidth];
    const int16_t previewWidth = static_cast<int16_t>(w - 12);
    for (int16_t rowY = 0; rowY < imgH; ++rowY) {
      const int16_t sourceY = rowY * kModePreviewHeight / imgH;
      for (int16_t col = 0; col < previewWidth; ++col) {
        const int16_t sourceX = col * kModePreviewWidth / previewWidth;
        row[col] = pgm_read_word(
            &mode.preview[sourceY * kModePreviewWidth + sourceX]);
      }
      c.blitRgb565(static_cast<int16_t>(x + 6),
                   static_cast<int16_t>(y + 6 + rowY), previewWidth, 1, row);
    }
  }

  const int16_t titleY = static_cast<int16_t>(y + imgH + 12);
  const int16_t titleHeight = static_cast<int16_t>(h - imgH - 20);
  Brand::drawTitleIn(
      c, mode.label,
      Rect(static_cast<int16_t>(x + 8), titleY,
           static_cast<int16_t>(w - 16), titleHeight),
      Somnium::Ui::Easing::lerpColor(T().textMuted, T().text, emphasis));
}

void ModePage::drawCarousel(Somnium::Ui::Canvas &c) {
  c.fillRect(Rect(0, 22, c.width(), static_cast<int16_t>(c.height() - 22)),
             T().bg);
  int slots[] = {-3, -2, -1, 0, 1, 2, 3};
  for (int i = 1; i < 7; ++i) {
    const int slot = slots[i];
    int j = i;
    while (j > 0 && fabsf(slots[j - 1] + slide_) < fabsf(slot + slide_)) {
      slots[j] = slots[j - 1];
      --j;
    }
    slots[j] = slot;
  }
  for (int slot : slots) {
    drawPanel(c, index_ + slot, static_cast<float>(slot), slide_);
  }
  const char *hint = Somnium::Ble::pairPending()
                         ? "hold shutter or click to pair"
                         : "turn: mode | hold shutter: storage";
  Text::drawIn(c, hint, Rect(0, static_cast<int16_t>(c.height() - 12), c.width(), 12),
               mutedCenter());
}

void ModePage::draw(Somnium::Ui::Canvas &c) {
  if (headerOnly_) {
    drawHeaderOnly(c);
    headerOnly_ = false;
    return;
  }
  c.clear(T().bg);
  drawHeaderOnly(c);
  drawCarousel(c);
}

void ProcessPage::onEnter() {
  elapsed_ = 0;
  animationMs_ = 0;
  failed_ = false;
  strncpy(status_, "warming up...", sizeof(status_) - 1);
  status_[sizeof(status_) - 1] = '\0';
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
  Text::drawIn(c, "cam > phone > dream", Rect(0, 2, c.width(), 10), mutedCenter());

  const int16_t mid = static_cast<int16_t>(c.width() / 2);
  const int16_t y = 36;
  KitchenAnim::drawFire(c, static_cast<int16_t>(mid - 70), y, elapsed_, T().text);
  KitchenAnim::drawSalt(c, mid, y, elapsed_, T().pinkDim);
  KitchenAnim::drawWhisk(c, static_cast<int16_t>(mid + 70), y, elapsed_, T().text);

  Text::drawIn(c, failed_ ? "Kitchen trouble" : "Magic is Happening...",
               Rect(0, 70, c.width(), 12), TextStyle(T().text, 1, Align::Center));
  Text::drawIn(c, status_, Rect(0, 86, c.width(), 12), mutedCenter());
  if (label_ != nullptr && label_[0] != '\0') {
    Text::drawIn(c, label_, Rect(0, 100, c.width(), 12), mutedCenter());
  }
  Brand::drawBattery(c, T());
  if (failed_) {
    Text::drawIn(c, "hold shutter or click", Rect(0, static_cast<int16_t>(c.height() - 14),
                                                 c.width(), 12),
                 mutedCenter());
  }
}

void PreviewPage::releaseRgb() {
  free(rgb_);
  rgb_ = nullptr;
  rgbW_ = 0;
  rgbH_ = 0;
  rgbOk_ = false;
}

bool PreviewPage::ensureRgb(int16_t w, int16_t h) {
  if (rgbOk_ && rgb_ != nullptr && rgbW_ == w && rgbH_ == h) {
    return true;
  }
  if (jpeg_ == nullptr || jpegLen_ == 0) {
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
  rgbOk_ = Somnium::Image::decodeRgb565(jpeg_, jpegLen_, rgb_, w, h);
  if (!rgbOk_) {
    releaseRgb();
  }
  return rgbOk_;
}

void PreviewPage::beginReview(const char *modeLabel, const uint8_t *jpeg,
                              size_t length) {
  kind_ = Kind::Review;
  modeLabel_ = modeLabel != nullptr ? modeLabel : "";
  index_ = 0;
  total_ = 0;
  back_ = false;
  autoDone_ = false;
  reviewElapsed_ = 0;
  setPhoto(jpeg, length);
}

void PreviewPage::beginStorage(int index0, int total) {
  kind_ = Kind::Storage;
  index_ = total > 0 ? index0 + 1 : 0;
  total_ = total;
  back_ = false;
  autoDone_ = false;
}

void PreviewPage::setPhoto(const uint8_t *jpeg, size_t length) {
  releaseRgb();
  jpeg_ = jpeg;
  jpegLen_ = length;
  decodePending_ = jpeg_ != nullptr && jpegLen_ > 0;
  allowDecode_ = false;
  previewPresented_ = false;
  markDirty();
}

void PreviewPage::setMeta(const char *modeLabel, int index1Based, int total) {
  modeLabel_ = modeLabel != nullptr ? modeLabel : "";
  index_ = index1Based;
  total_ = total;
  markDirty();
}

void PreviewPage::onEnter() {
  back_ = false;
  autoDone_ = false;
  previewPresented_ = false;
  reviewElapsed_ = 0;
  clockMs_ = 0;
  markDirty();
}

void PreviewPage::onExit() { releaseRgb(); }

void PreviewPage::update(uint32_t dtMs) {
  clockMs_ += dtMs;
  if (kind_ == Kind::Review && previewPresented_) {
    reviewElapsed_ += dtMs;
    if (reviewElapsed_ >= kReviewMs) {
      autoDone_ = true;
    }
  }
  if (clockMs_ >= 1000) {
    clockMs_ = 0;
    markDirty();
  }
  if (decodePending_ && allowDecode_ && !rgbOk_) {
    decodePending_ = false;
    if (!ensureRgb(Somnium::Defaults::kDisplayWidth,
                   Somnium::Defaults::kDisplayHeight)) {
      Somnium::Console::printf("JPEG decode failed %u B\n",
                               static_cast<unsigned>(jpegLen_));
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
  // Navigation is owned by App (shutter = shoot, dial-click = Modes↔Storage).
  // Only honor Back here; Select is ignored so shutter never means "exit".
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
  }
  return false;
}

void PreviewPage::drawChrome(Somnium::Ui::Canvas &c) {
  c.fillRect(Rect(0, 0, c.width(), 14), T().surface);
  Text::drawIn(c, "hold shutter: back", Rect(4, 2, static_cast<int16_t>(c.width() - 70), 8),
               TextStyle(T().text, 1));
  Brand::drawBattery(c, T());

  c.fillRect(Rect(0, static_cast<int16_t>(c.height() - 16), c.width(), 16),
             T().surface);

  char when[32];
  Chrono::formatDateTime(when, sizeof(when));
  const int16_t clockWidth = Text::measure(when, 1).w;
  const int16_t clockX = static_cast<int16_t>(c.width() - clockWidth - 8);
  const Rect metadataBox(4, static_cast<int16_t>(c.height() - 12),
                         static_cast<int16_t>(clockX - 8), 8);
  if (kind_ == Kind::Storage) {
    char meta[40];
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
               Rect(clockX, static_cast<int16_t>(c.height() - 12), clockWidth, 8),
               TextStyle(T().text, 1));
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
    reviewElapsed_ = 0;
  }
  drawChrome(c);
}

}  // namespace App
}  // namespace Somnium
