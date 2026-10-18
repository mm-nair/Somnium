#include "Pages.h"

#include <Image.h>
#include <Ui.h>
#include <Wifi.h>
#include <esp_heap_caps.h>
#include <math.h>
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
  Brand::drawLogo(c, static_cast<int16_t>((c.width() - kLogoW) / 2), 28);

  // Same L→R trio as Process: fire · salt · whisk
  KitchenAnim::drawFire(c, 72, 118, elapsed_, T().pinkDim);
  KitchenAnim::drawSalt(c, 160, 108, elapsed_, T().text);
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
  openStorage_ = false;
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
    pendingNudge_ += delta > 0 ? 1 : -1;
    return;
  }
  index_ += delta > 0 ? 1 : -1;
  slideTween_.start(delta > 0 ? 1.0f : -1.0f, 0.0f, 360, millis(),
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
  char when[32];
  Chrono::formatDateTime(when, sizeof(when));
  Brand::drawHeader(c, T(), "Storage", Somnium::Wifi::isConnected(), when);
}

void ModePage::drawPanel(Somnium::Ui::Canvas &c, int modeIndex, float slot,
                         float slide) {
  const float xNorm = slot + slide;
  const float depth = fabsf(xNorm);
  const float scale = 1.0f - depth * 0.22f;
  const int16_t w = static_cast<int16_t>(150 * scale);
  const int16_t h = static_cast<int16_t>(150 * scale);
  const int16_t cx = static_cast<int16_t>(c.width() / 2 + xNorm * 95);
  const int16_t cy = static_cast<int16_t>(118 + depth * 10);
  const int16_t x = static_cast<int16_t>(cx - w / 2);
  const int16_t y = static_cast<int16_t>(cy - h / 2);

  const Mode &mode = modeAt(modeIndex);
  const bool front = depth < 0.35f;
  const Color frame = front ? T().text : T().pinkDim;
  c.fillRect(Rect(x, y, w, h), T().surfaceAlt);
  c.drawRect(Rect(x, y, w, h), frame);

  const int16_t imgH = static_cast<int16_t>(h * 0.55f);
  c.fillRect(Rect(static_cast<int16_t>(x + 6), static_cast<int16_t>(y + 6),
                  static_cast<int16_t>(w - 12), imgH),
             Color(mode.placeholderColor));

  if (front) {
    Brand::drawTitleFont(c, mode.label, static_cast<int16_t>(x + 18),
                         static_cast<int16_t>(y + h - 28), T().text);
  } else {
    Text::drawIn(c, mode.label,
                 Rect(x, static_cast<int16_t>(y + h - 28), w, 20),
                 mutedCenter());
  }
}

void ModePage::drawCarousel(Somnium::Ui::Canvas &c) {
  c.fillRect(Rect(0, 22, c.width(), static_cast<int16_t>(c.height() - 22)),
             T().bg);
  drawPanel(c, index_ - 2, -2.0f, slide_);
  drawPanel(c, index_ + 2, 2.0f, slide_);
  drawPanel(c, index_ - 1, -1.0f, slide_);
  drawPanel(c, index_ + 1, 1.0f, slide_);
  drawPanel(c, index_, 0.0f, slide_);
  Text::drawIn(c, "dial turn modes · click storage · shutter shoot",
               Rect(0, 220, c.width(), 14), mutedCenter());
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
  failed_ = false;
  strncpy(status_, "warming up…", sizeof(status_) - 1);
  status_[sizeof(status_) - 1] = '\0';
  markDirty();
}

void ProcessPage::setStatus(const char *status) {
  strncpy(status_, status != nullptr ? status : "", sizeof(status_) - 1);
  status_[sizeof(status_) - 1] = '\0';
  if (strstr(status_, "fail") != nullptr || strstr(status_, "no ") != nullptr) {
    failed_ = true;
  }
  markDirty();
}

void ProcessPage::update(uint32_t dtMs) {
  elapsed_ += dtMs;
  markDirty();
}

void ProcessPage::draw(Somnium::Ui::Canvas &c) {
  c.clear(T().bg);
  Text::drawIn(c, "cam > wifi > dream", Rect(0, 40, c.width(), 14),
               mutedCenter());

  // Same L→R order as Boot: fire · salt · whisk (no progress bar).
  const int16_t y = 88;
  c.fillRect(Rect(36, y, 40, 40), T().surfaceAlt);
  c.fillRect(Rect(140, y, 40, 40), T().surfaceAlt);
  c.fillRect(Rect(244, y, 40, 40), T().surfaceAlt);
  c.hLine(76, static_cast<int16_t>(y + 20), 64, T().text);
  c.hLine(180, static_cast<int16_t>(y + 20), 64, T().text);
  KitchenAnim::drawFire(c, 56, static_cast<int16_t>(y + 14), elapsed_, T().text);
  KitchenAnim::drawSalt(c, 160, static_cast<int16_t>(y + 12), elapsed_,
                        T().pinkDim);
  KitchenAnim::drawWhisk(c, 264, static_cast<int16_t>(y + 10), elapsed_,
                         T().text);

  Text::drawIn(c, failed_ ? "Kitchen trouble" : "Magic is Happening...",
               Rect(0, 160, c.width(), 20), TextStyle(T().text, 2, Align::Center));
  Text::drawIn(c, status_, Rect(0, 188, c.width(), 14), mutedCenter());
  if (label_ != nullptr && label_[0] != '\0') {
    Text::drawIn(c, label_, Rect(0, 208, c.width(), 14), mutedCenter());
  }
  if (failed_) {
    Text::drawIn(c, "dial-click back", Rect(0, 224, c.width(), 12),
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
  setPhoto(jpeg, length);
}

void PreviewPage::beginStorage(int index0, int total) {
  kind_ = Kind::Storage;
  index_ = total > 0 ? index0 + 1 : 0;
  total_ = total;
  back_ = false;
}

void PreviewPage::setPhoto(const uint8_t *jpeg, size_t length) {
  releaseRgb();
  jpeg_ = jpeg;
  jpegLen_ = length;
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
  clockMs_ = 0;
  markDirty();
}

void PreviewPage::onExit() { releaseRgb(); }

void PreviewPage::update(uint32_t dtMs) {
  clockMs_ += dtMs;
  if (clockMs_ >= 1000) {
    clockMs_ = 0;
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
  c.fillRect(Rect(0, 0, c.width(), 22), T().surface);
  Text::draw(c, "dial-click back", 6, 7, TextStyle(T().text, 1));

  c.fillRect(Rect(0, static_cast<int16_t>(c.height() - 28), c.width(), 28),
             T().surface);

  if (kind_ == Kind::Storage) {
    char meta[40];
    if (total_ <= 0) {
      snprintf(meta, sizeof(meta), "no plates yet");
    } else {
      snprintf(meta, sizeof(meta), "%s  %d/%d", modeLabel_, index_, total_);
    }
    Text::draw(c, meta, 8, static_cast<int16_t>(c.height() - 18),
               TextStyle(T().text, 1));
  } else {
    Text::draw(c, modeLabel_ != nullptr ? modeLabel_ : "dream", 8,
               static_cast<int16_t>(c.height() - 18), TextStyle(T().text, 1));
  }

  char when[32];
  Chrono::formatDateTime(when, sizeof(when));
  const int16_t tw = Text::measure(when, 1).w;
  Text::draw(c, when, static_cast<int16_t>(c.width() - tw - 8),
             static_cast<int16_t>(c.height() - 18), TextStyle(T().text, 1));
  c.hLine(static_cast<int16_t>(c.width() - tw - 8),
          static_cast<int16_t>(c.height() - 8), tw, T().pinkDim);
}

void PreviewPage::draw(Somnium::Ui::Canvas &c) {
  c.clear(T().bg);
  if (jpeg_ != nullptr && jpegLen_ > 0) {
    // Decode once into RGB565 — re-decoding every frame locks the UI (dial dies).
    if (ensureRgb(c.width(), c.height())) {
      c.blitRgb565(0, 0, rgbW_, rgbH_, rgb_);
    } else {
      Somnium::Image::drawJpeg(jpeg_, jpegLen_, c.gfx());
    }
  } else if (kind_ == Kind::Storage && total_ <= 0) {
    Text::drawIn(c, "no plates yet", Rect(0, 110, c.width(), 20), mutedCenter());
  } else {
    Text::drawIn(c, "plating...", Rect(0, 110, c.width(), 20), mutedCenter());
  }
  drawChrome(c);
}

}  // namespace App
}  // namespace Somnium
