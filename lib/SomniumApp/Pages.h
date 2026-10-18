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
  bool openStorageRequested() const { return openStorage_; }
  void clearOpenStorage() { openStorage_ = false; }

  void nudge(int delta);

 private:
  void drawHeaderOnly(Somnium::Ui::Canvas &c);
  void drawCarousel(Somnium::Ui::Canvas &c);
  void drawPanel(Somnium::Ui::Canvas &c, int modeIndex, float slot,
                 float slide);

  int index_ = 0;
  int pendingNudge_ = 0;
  float slide_ = 0;
  Somnium::Ui::Tween slideTween_{};
  bool takePhoto_ = false;
  bool openStorage_ = false;
  uint32_t clockMs_ = 0;
  bool headerOnly_ = false;
};

class ProcessPage : public Somnium::Ui::Page {
 public:
  void onEnter() override;
  void update(uint32_t dtMs) override;
  void draw(Somnium::Ui::Canvas &canvas) override;
  void setModeLabel(const char *label) { label_ = label; }
  bool readyToWork() const { return elapsed_ > 900; }

 private:
  uint32_t elapsed_ = 0;
  const char *label_ = "";
};

// Shared full-screen photo view:
//   Review  — post-shot glance (auto-returns to Modes, no gallery chrome)
//   Storage — browse saved plates with dial; footer shows i/len(photos)
class PreviewPage : public Somnium::Ui::Page {
 public:
  enum class Kind : uint8_t { Review, Storage };

  void onEnter() override;
  void update(uint32_t dtMs) override;
  void draw(Somnium::Ui::Canvas &canvas) override;
  bool handleAction(Somnium::Ui::NavAction action) override;

  void beginReview(const char *modeLabel, const uint8_t *jpeg, size_t length);
  void beginStorage(int index, int total);
  void setPhoto(const uint8_t *jpeg, size_t length);
  void setMeta(const char *modeLabel, int index1Based, int total);

  void nudge(int delta);

  Kind kind() const { return kind_; }
  int storageIndex() const { return index_ - 1; }  // 0-based
  bool backRequested() const { return back_; }
  void clearBack() { back_ = false; }
  bool autoDone() const { return autoDone_; }

 private:
  void drawChrome(Somnium::Ui::Canvas &c);

  Kind kind_ = Kind::Review;
  const char *modeLabel_ = "";
  int index_ = 1;  // 1-based for display
  int total_ = 0;
  bool back_ = false;
  bool autoDone_ = false;
  const uint8_t *jpeg_ = nullptr;
  size_t jpegLen_ = 0;
  uint32_t elapsed_ = 0;
  uint32_t clockMs_ = 0;
};

}  // namespace App
}  // namespace Somnium
