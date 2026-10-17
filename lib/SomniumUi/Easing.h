#pragma once

#include <math.h>

#include "Types.h"

namespace Somnium {
namespace Ui {

using EaseFn = float (*)(float);

struct Easing {
  static float linear(float t) { return t; }

  static float easeOutCubic(float t) {
    const float u = 1.0f - t;
    return 1.0f - u * u * u;
  }

  static float easeInOutQuad(float t) {
    return t < 0.5f ? 2.0f * t * t : 1.0f - 2.0f * (1.0f - t) * (1.0f - t);
  }

  static float easeOutBack(float t) {
    constexpr float c1 = 1.70158f;
    constexpr float c3 = c1 + 1.0f;
    const float u = t - 1.0f;
    return 1.0f + c3 * u * u * u + c1 * u * u;
  }

  static float clamp01(float t) {
    if (t <= 0.0f) {
      return 0.0f;
    }
    if (t >= 1.0f) {
      return 1.0f;
    }
    return t;
  }

  static float lerp(float a, float b, float t) { return a + (b - a) * t; }

  static int16_t lerpI(int16_t a, int16_t b, float t) {
    return static_cast<int16_t>(lroundf(lerp(a, b, t)));
  }

  static Color lerpColor(Color a, Color b, float t) {
    const auto ch = [](uint16_t c, int shift, int bits) -> float {
      const uint16_t mask = static_cast<uint16_t>((1u << bits) - 1u);
      return static_cast<float>((c >> shift) & mask);
    };
    const float r = lerp(ch(a.value, 11, 5), ch(b.value, 11, 5), t);
    const float g = lerp(ch(a.value, 5, 6), ch(b.value, 5, 6), t);
    const float bl = lerp(ch(a.value, 0, 5), ch(b.value, 0, 5), t);
    return Color(static_cast<uint16_t>((static_cast<uint16_t>(r) << 11) |
                                       (static_cast<uint16_t>(g) << 5) |
                                       static_cast<uint16_t>(bl)));
  }
};

}  // namespace Ui
}  // namespace Somnium
