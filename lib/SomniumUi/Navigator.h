#pragma once

#include "Canvas.h"
#include "Page.h"
#include "Theme.h"
#include "Tween.h"

namespace Somnium {
namespace Ui {

enum class Transition : uint8_t {
  None,
  FadeThrough,
  SlideLeft,
  SlideRight,
};

class Navigator {
 public:
  static constexpr uint8_t kMaxDepth = 6;

  void begin(Canvas *canvas, const Theme *theme);

  bool push(Page *page, Transition transition = Transition::None,
            uint16_t durationMs = 0);
  bool replace(Page *page, Transition transition = Transition::None,
               uint16_t durationMs = 0);
  bool pop(Transition transition = Transition::None, uint16_t durationMs = 0);

  Page *current() const;
  uint8_t depth() const { return depth_; }
  bool busy() const { return transition_.progress.active; }

  void update(uint32_t nowMs);
  void draw();
  void invalidate();
  // Drop the sleep gap so one frame does not fast-forward tweens.
  void rebaseClock();
  bool handleAction(NavAction action);

 private:
  struct Frame {
    Page *from = nullptr;
    Page *to = nullptr;
    Transition type = Transition::None;
    Tween progress;
  };

  void startTransition(Page *from, Page *to, Transition type,
                       uint16_t durationMs);
  void finishTransition();
  uint16_t resolveDuration(uint16_t durationMs) const;
  void drawTransition();

  Canvas *canvas_ = nullptr;
  const Theme *theme_ = nullptr;
  Page *stack_[kMaxDepth] = {};
  uint8_t depth_ = 0;
  Frame transition_{};
  uint32_t lastMs_ = 0;
  bool needsDraw_ = true;
};

}  // namespace Ui
}  // namespace Somnium
