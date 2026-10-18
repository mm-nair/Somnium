"""Host-side check of the firmware Li-ion curve. No board required."""

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]

SOURCE = r"""
#include "BatteryCurve.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

static void expect(bool ok, const char *message) {
  if (!ok) {
    std::fprintf(stderr, "%s\n", message);
    std::exit(1);
  }
}

int main() {
  int count = 0;
  const Somnium::VoltagePoint *curve = Somnium::batteryCurve(&count);
  expect(count >= 2, "curve length");
  for (int i = 1; i < count; ++i) {
    expect(curve[i].volts > curve[i - 1].volts, "voltage order");
    expect(curve[i].percent >= curve[i - 1].percent, "percent order");
  }
  expect(Somnium::percentFromVoltage(3.2f) == 0.0f, "empty");
  expect(Somnium::percentFromVoltage(2.5f) == 0.0f, "below");
  expect(Somnium::percentFromVoltage(4.2f) == 100.0f, "full");
  expect(Somnium::percentFromVoltage(4.5f) == 100.0f, "above");
  expect(Somnium::percentFromVoltage(3.7f) == 45.0f, "knot");
  const float mid = Somnium::percentFromVoltage(3.75f);
  expect(std::fabs(mid - 52.5f) < 0.01f, "lerp");

  const float volts = Somnium::voltageFromDivider(2048, 3.1f, 2.0f, 4095);
  const float expected = (2048.0f / 4095.0f) * 3.1f * 2.0f;
  expect(std::fabs(volts - expected) < 0.0001f, "divider");
  expect(Somnium::voltageFromDivider(10, 3.1f, 2.0f, 0) == 0.0f, "bad adc");
  expect(Somnium::voltageFromDivider(-5, 3.1f, 2.0f, 4095) == 0.0f, "clamp low");
  return 0;
}
"""


class BatteryCurveTests(unittest.TestCase):
    def test_curve_and_divider(self):
        if shutil.which("g++") is None:
            self.skipTest("g++ not installed")
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            source = directory / "curve.cpp"
            source.write_text(SOURCE)
            binary = directory / "curve"
            subprocess.run(
                [
                    "g++",
                    "-std=c++11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT / "lib/Somnium"),
                    str(source),
                    "-o",
                    str(binary),
                ],
                check=True,
                text=True,
            )
            subprocess.run([str(binary)], check=True, text=True)


if __name__ == "__main__":
    unittest.main()
