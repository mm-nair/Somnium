#pragma once

#include "Canvas.h"
#include "Navigator.h"
#include "Theme.h"

namespace Somnium {
namespace Ui {

class Ui {
 public:
  static bool begin();
  static void update();
  static void draw();
  static void tick();

  static Canvas &canvas();
  static Navigator &navigator();
  static Theme &theme();

  static bool push(Page *page, Transition transition = Transition::FadeThrough,
                   uint16_t durationMs = 0);
  static bool replace(Page *page, Transition transition = Transition::FadeThrough,
                      uint16_t durationMs = 0);
  static bool pop(Transition transition = Transition::FadeThrough,
                  uint16_t durationMs = 0);
  static bool handleAction(NavAction action);

 private:
  static bool ready_;
  static Theme theme_;
  static Canvas canvas_;
  static Navigator navigator_;
};

}  // namespace Ui
}  // namespace Somnium
