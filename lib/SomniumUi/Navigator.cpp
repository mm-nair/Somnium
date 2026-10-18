#include "Navigator.h"

namespace Somnium {
namespace Ui {

void Navigator::begin(Canvas *canvas, const Theme *theme) {
  canvas_ = canvas;
  theme_ = theme;
  depth_ = 0;
  transition_ = {};
  lastMs_ = millis();
  needsDraw_ = true;
}

Page *Navigator::current() const {
  return depth_ > 0 ? stack_[depth_ - 1] : nullptr;
}

uint16_t Navigator::resolveDuration(uint16_t durationMs) const {
  if (durationMs > 0) {
    return durationMs;
  }
  return theme_ != nullptr ? theme_->transitionMs : 220;
}

void Navigator::startTransition(Page *from, Page *to, Transition type,
                                uint16_t durationMs) {
  if (type == Transition::None || from == nullptr || to == nullptr ||
      from == to) {
    if (from != nullptr && from != to) {
      from->onExit();
    }
    if (to != nullptr) {
      to->onEnter();
      to->markDirty();
    }
    transition_ = {};
    needsDraw_ = true;
    return;
  }

  transition_.from = from;
  transition_.to = to;
  transition_.type = type;
  transition_.progress.start(0.0f, 1.0f, resolveDuration(durationMs), millis(),
                             Easing::easeInOutQuad);
  needsDraw_ = true;
}

void Navigator::finishTransition() {
  Page *from = transition_.from;
  Page *to = transition_.to;
  if (from != nullptr) {
    from->onExit();
  }
  if (to != nullptr) {
    to->onEnter();
    to->markDirty();
  }
  transition_ = {};
  needsDraw_ = true;
}

bool Navigator::push(Page *page, Transition transition, uint16_t durationMs) {
  if (page == nullptr || depth_ >= kMaxDepth || busy()) {
    return false;
  }
  Page *from = current();
  stack_[depth_++] = page;
  startTransition(from, page, transition, durationMs);
  return true;
}

bool Navigator::replace(Page *page, Transition transition, uint16_t durationMs) {
  if (page == nullptr || depth_ == 0 || busy()) {
    return false;
  }
  Page *from = current();
  stack_[depth_ - 1] = page;
  startTransition(from, page, transition, durationMs);
  return true;
}

bool Navigator::pop(Transition transition, uint16_t durationMs) {
  if (depth_ <= 1 || busy()) {
    return false;
  }
  Page *from = current();
  --depth_;
  Page *to = current();
  startTransition(from, to, transition, durationMs);
  return true;
}

bool Navigator::handleAction(NavAction action) {
  if (busy() || current() == nullptr) {
    return false;
  }
  return current()->handleAction(action);
}

void Navigator::update(uint32_t nowMs) {
  const uint32_t dt = lastMs_ == 0 ? 0 : nowMs - lastMs_;
  lastMs_ = nowMs;

  if (transition_.progress.active) {
    if (transition_.progress.update(nowMs)) {
      needsDraw_ = true;
    }
    if (transition_.progress.finished) {
      finishTransition();
    }
    return;
  }

  Page *page = current();
  if (page == nullptr) {
    return;
  }
  page->update(dt);
  if (page->isDirty()) {
    needsDraw_ = true;
  }
}

void Navigator::drawTransition() {
  if (canvas_ == nullptr) {
    return;
  }
  const float t = transition_.progress.value;
  Page *from = transition_.from;
  Page *to = transition_.to;
  const Color bg = theme_ != nullptr ? theme_->bg : Color(0);

  canvas_->resetOrigin();
  canvas_->resetClip();

  switch (transition_.type) {
    case Transition::SlideLeft:
    case Transition::SlideRight: {
      const int16_t w = canvas_->width();
      const int dir = transition_.type == Transition::SlideLeft ? -1 : 1;
      const int16_t fromX = Easing::lerpI(0, static_cast<int16_t>(dir * w), t);
      const int16_t toX = Easing::lerpI(static_cast<int16_t>(-dir * w), 0, t);
      canvas_->fillScreen(bg);
      if (from != nullptr) {
        canvas_->setOrigin(fromX, 0);
        canvas_->setClip(Rect::intersect(
            canvas_->bounds(), Rect(fromX, 0, w, canvas_->height())));
        from->draw(*canvas_);
      }
      if (to != nullptr) {
        canvas_->setOrigin(toX, 0);
        canvas_->setClip(Rect::intersect(Rect(0, 0, w, canvas_->height()),
                                         Rect(toX, 0, w, canvas_->height())));
        to->draw(*canvas_);
      }
      canvas_->resetOrigin();
      canvas_->resetClip();
      break;
    }
    case Transition::FadeThrough:
    case Transition::None:
    default:
      // No black wipe — present the destination page in one buffered frame.
      if (to != nullptr) {
        to->draw(*canvas_);
      } else if (from != nullptr) {
        from->draw(*canvas_);
      }
      break;
  }
}

void Navigator::draw() {
  if (canvas_ == nullptr || !needsDraw_) {
    return;
  }
  needsDraw_ = false;

  canvas_->beginFrame();
  if (transition_.progress.active) {
    drawTransition();
  } else {
    Page *page = current();
    if (page != nullptr) {
      canvas_->resetOrigin();
      canvas_->resetClip();
      page->draw(*canvas_);
      page->consumeDirty();
    }
  }
  canvas_->endFrame();
}

}  // namespace Ui
}  // namespace Somnium
