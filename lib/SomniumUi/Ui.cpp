#include "Ui.h"

#include <Display.h>

namespace Somnium {
namespace Ui {

bool Ui::ready_ = false;
Theme Ui::theme_{};
Canvas Ui::canvas_{};
Navigator Ui::navigator_{};

bool Ui::begin() {
  if (ready_) {
    return true;
  }
  if (!Display::ready() || Display::tft() == nullptr) {
    return false;
  }
  canvas_.bind(Display::tft(), static_cast<int16_t>(Display::width()),
               static_cast<int16_t>(Display::height()));
  // Drawing goes to a PSRAM backbuffer; Navigator flushes once per frame.
  navigator_.begin(&canvas_, &theme_);
  ready_ = true;
  return true;
}

void Ui::update() {
  if (ready_) {
    navigator_.update(millis());
  }
}

void Ui::draw() {
  if (ready_) {
    navigator_.draw();
  }
}

void Ui::tick() {
  update();
  draw();
}

void Ui::invalidate() {
  if (ready_) {
    navigator_.invalidate();
  }
}

void Ui::rebaseClock() {
  if (ready_) {
    navigator_.rebaseClock();
  }
}

Canvas &Ui::canvas() { return canvas_; }

Navigator &Ui::navigator() { return navigator_; }

Theme &Ui::theme() { return theme_; }

bool Ui::push(Page *page, Transition transition, uint16_t durationMs) {
  return ready_ && navigator_.push(page, transition, durationMs);
}

bool Ui::replace(Page *page, Transition transition, uint16_t durationMs) {
  return ready_ && navigator_.replace(page, transition, durationMs);
}

bool Ui::pop(Transition transition, uint16_t durationMs) {
  return ready_ && navigator_.pop(transition, durationMs);
}

bool Ui::handleAction(NavAction action) {
  return ready_ && navigator_.handleAction(action);
}

}  // namespace Ui
}  // namespace Somnium
