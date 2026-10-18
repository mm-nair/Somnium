#pragma once

// Host-testable Li-ion state of charge. No Arduino headers.

namespace Somnium {

struct VoltagePoint {
  float volts;
  float percent;
};

// Resting 1S Li-ion, piecewise linear. Not a load-compensated gauge.
inline const VoltagePoint *batteryCurve(int *count) {
  static const VoltagePoint kCurve[] = {
      {3.20f, 0.0f},  {3.30f, 5.0f},  {3.40f, 10.0f}, {3.50f, 20.0f},
      {3.60f, 30.0f}, {3.70f, 45.0f}, {3.80f, 60.0f}, {3.90f, 70.0f},
      {4.00f, 80.0f}, {4.10f, 90.0f}, {4.20f, 100.0f},
  };
  if (count != nullptr) {
    *count = static_cast<int>(sizeof(kCurve) / sizeof(kCurve[0]));
  }
  return kCurve;
}

inline float percentFromVoltage(float voltage) {
  int count = 0;
  const VoltagePoint *curve = batteryCurve(&count);
  if (count <= 0) {
    return 0.0f;
  }
  if (voltage <= curve[0].volts) {
    return curve[0].percent;
  }
  if (voltage >= curve[count - 1].volts) {
    return curve[count - 1].percent;
  }
  for (int i = 1; i < count; ++i) {
    if (voltage <= curve[i].volts) {
      const float span = curve[i].volts - curve[i - 1].volts;
      const float t = span <= 0.0f ? 0.0f : (voltage - curve[i - 1].volts) / span;
      return curve[i - 1].percent +
             (curve[i].percent - curve[i - 1].percent) * t;
    }
  }
  return curve[count - 1].percent;
}

// raw/adcMax is the fraction of the ADC full-scale voltage, then scaled by the
// divider ratio (Vbat = Vadc * (R1+R2)/R2).
inline float voltageFromDivider(int raw, float fullScaleVolts, float ratio,
                                int adcMax) {
  if (adcMax <= 0 || ratio <= 0.0f || fullScaleVolts <= 0.0f) {
    return 0.0f;
  }
  if (raw < 0) {
    raw = 0;
  }
  if (raw > adcMax) {
    raw = adcMax;
  }
  const float vadc =
      (static_cast<float>(raw) / static_cast<float>(adcMax)) * fullScaleVolts;
  return vadc * ratio;
}

}  // namespace Somnium
