#pragma once

#include "Easing.h"

namespace Somnium {
namespace Ui {

struct Tween {
  float from = 0;
  float to = 1;
  float value = 0;
  uint32_t startMs = 0;
  uint16_t durationMs = 0;
  EaseFn ease = Easing::easeOutCubic;
  bool active = false;
  bool finished = false;

  void start(float fromValue, float toValue, uint16_t duration, uint32_t nowMs,
             EaseFn easeFn = Easing::easeOutCubic) {
    from = fromValue;
    to = toValue;
    value = fromValue;
    startMs = nowMs;
    durationMs = duration;
    ease = easeFn != nullptr ? easeFn : Easing::linear;
    active = duration > 0;
    finished = duration == 0;
    if (finished) {
      value = toValue;
    }
  }

  bool update(uint32_t nowMs) {
    if (!active) {
      return false;
    }
    if (durationMs == 0) {
      value = to;
      active = false;
      finished = true;
      return true;
    }
    const float t = Easing::clamp01(static_cast<float>(nowMs - startMs) /
                                    static_cast<float>(durationMs));
    value = Easing::lerp(from, to, ease(t));
    if (t >= 1.0f) {
      value = to;
      active = false;
      finished = true;
    }
    return true;
  }
};

}  // namespace Ui
}  // namespace Somnium
