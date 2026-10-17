#pragma once

#include <Arduino.h>

#include "Config.h"

namespace Somnium {

enum class InputEvent : uint8_t {
  None = 0,
  ShutterPressed,
  ShutterReleased,
  EncoderPressed,
  EncoderReleased,
  EncoderTurned,  // delta in Inputs::lastEncoderDelta()
};

// Debounced shutter + encoder (quadrature + push). Active LOW with pull-ups.
class Inputs {
 public:
  static void begin();
  static void update();

  static bool shutterDown();
  static bool encoderButtonDown();
  static long encoderPosition();
  static int8_t lastEncoderDelta();

  // Returns the next pending event since the previous call (queue of 1 kind
  // each — call repeatedly until None if you need to drain a burst).
  static InputEvent pollEvent();

 private:
  struct Button {
    uint8_t pin;
    int lastReading;
    int stableReading;
    unsigned long lastChangedAt;
    bool pressEvent;
    bool releaseEvent;
  };

  static void initButton(Button &button, uint8_t pin);
  static void updateButton(Button &button);
  static void updateEncoder();

  static Button sShutter;
  static Button sEncoderSw;
  static uint8_t sPrevEncoderState;
  static int sEncoderTransitions;
  static long sEncoderPosition;
  static int8_t sLastEncoderDelta;
  static bool sEncoderTurnEvent;
};

}  // namespace Somnium
