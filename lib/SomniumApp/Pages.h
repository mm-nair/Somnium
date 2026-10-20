#pragma once

#include <Page.h>
#include <Tween.h>

#include "Modes.h"

namespace Somnium {
namespace App {

class BootPage : public Somnium::Ui::Page {
 public:
  void onEnter() override;
  void update(uint32_t dtMs) override;
  void draw(Somnium::Ui::Canvas &canvas) override;
  bool finished() const { return done_; }

 private:
  uint32_t elapsed_ = 0;
  bool done_ = false;
};

// Lookbook. The dial moves between creative.json modes; the hero and the
// filmstrip both use the generated preview bitmaps.
class ModePage : public Somnium::Ui::Page {
 public:
  void onEnter() override;
  void update(uint32_t dtMs) override;
  void draw(Somnium::Ui::Canvas &canvas) override;
  bool handleAction(Somnium::Ui::NavAction action) override;

  int selectedIndex() const { return index_; }
  const Mode &selected() const { return modeAt(index_); }
  bool takePhotoRequested() const { return takePhoto_; }
  void clearTakePhoto() { takePhoto_ = false; }
  bool openMenuRequested() const { return openMenu_; }
  void clearOpenMenu() { openMenu_ = false; }

  void nudge(int delta);

 private:
  void drawHeaderOnly(Somnium::Ui::Canvas &c);
  void drawLookbook(Somnium::Ui::Canvas &c);
  void drawHero(Somnium::Ui::Canvas &c, int modeIndex, int16_t xOffset,
                bool titled);
  void drawFilmstrip(Somnium::Ui::Canvas &c);

  int index_ = 0;
  int pendingNudge_ = 0;
  float slide_ = 0;
  Somnium::Ui::Tween slideTween_{};
  bool takePhoto_ = false;
  bool openMenu_ = false;
  uint32_t clockMs_ = 0;
  bool headerOnly_ = false;
};

// Click target from the lookbook. Shutter still shoots from here.
class MenuPage : public Somnium::Ui::Page {
 public:
  static constexpr int kGallery = 0;
  static constexpr int kShoot = 1;
  static constexpr int kBack = 2;
  static constexpr int kCount = 3;

  void onEnter() override;
  void update(uint32_t dtMs) override;
  void draw(Somnium::Ui::Canvas &canvas) override;
  void nudge(int delta);
  int selected() const { return index_; }
  void setContext(const char *lookLabel, int plates);

 private:
  int index_ = 0;
  char look_[20] = "";
  int plates_ = 0;
  uint32_t clockMs_ = 0;
};

class ProcessPage : public Somnium::Ui::Page {
 public:
  enum class CookStage : uint8_t { Capture = 0, Send = 1, Style = 2, Receive = 3 };

  void onEnter() override;
  void update(uint32_t dtMs) override;
  void draw(Somnium::Ui::Canvas &canvas) override;
  void setModeLabel(const char *label) { label_ = label; }
  void setStage(CookStage stage);
  CookStage stage() const { return stage_; }
  void setStatus(const char *status);
  bool readyToWork() const { return elapsed_ > 400; }
  bool failed() const { return failed_; }
  void setFailed() {
    failed_ = true;
    markDirty();
  }
  void clearFailed() { failed_ = false; }

 private:
  uint32_t elapsed_ = 0;
  uint32_t animationMs_ = 0;
  const char *label_ = "";
  char status_[48] = "capturing the frame";
  CookStage stage_ = CookStage::Capture;
  bool failed_ = false;
};

// Shared full-screen photo view:
//   Review  — dream just cooked; turn flips raw/dream; click returns home
//   Storage — one plate per shot; turn browses; click flips; hold returns
class PreviewPage : public Somnium::Ui::Page {
 public:
  enum class Kind : uint8_t { Review, Storage };
  enum class Side : uint8_t { Dream, Raw };

  void onEnter() override;
  void onExit() override;
  void update(uint32_t dtMs) override;
  void draw(Somnium::Ui::Canvas &canvas) override;
  bool handleAction(Somnium::Ui::NavAction action) override;

  void beginReview(const char *modeLabel, const uint8_t *jpeg, size_t length);
  void beginStorage(int index, int total);
  void setPhoto(const uint8_t *jpeg, size_t length);
  // dream is the styled JPEG, raw is the original. Either may be null.
  void setPair(const uint8_t *dream, size_t dreamLen, const uint8_t *raw,
               size_t rawLen, bool showRaw);
  void setAlternate(const uint8_t *jpeg, size_t length);
  void setMeta(const char *modeLabel, int index1Based, int total);
  bool flip();
  bool showingRaw() const { return side_ == Side::Raw; }

  void nudge(int delta);

  Kind kind() const { return kind_; }
  int storageIndex() const { return index_ - 1; }  // 0-based
  bool backRequested() const { return back_; }
  void clearBack() { back_ = false; }
  bool autoDone() const { return autoDone_; }

 private:
  void drawChrome(Somnium::Ui::Canvas &c);
  void releaseRgb();
  bool ensureRgb(int16_t w, int16_t h);
  const uint8_t *activeJpeg(size_t *length) const;

  Kind kind_ = Kind::Review;
  Side side_ = Side::Dream;
  const char *modeLabel_ = "";
  int index_ = 1;  // 1-based for display
  int total_ = 0;
  bool back_ = false;
  bool autoDone_ = false;
  const uint8_t *jpeg_ = nullptr;
  size_t jpegLen_ = 0;
  const uint8_t *alt_ = nullptr;
  size_t altLen_ = 0;
  uint16_t *rgb_ = nullptr;
  int16_t rgbW_ = 0;
  int16_t rgbH_ = 0;
  bool rgbOk_ = false;
  bool decodePending_ = false;
  bool allowDecode_ = false;
  bool previewPresented_ = false;
  uint32_t reviewStartedMs_ = 0;
  uint32_t clockMs_ = 0;
};

}  // namespace App
}  // namespace Somnium
