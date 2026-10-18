#pragma once

#include <Arduino.h>

namespace Somnium {

enum class InputEvent : uint8_t {
  None = 0,
  ShutterPressed,
  ShutterReleased,
  EncoderPressed,
  EncoderReleased,
  EncoderTurned,
};

class Inputs {
 public:
  static void begin();
  static void update();

  static bool shutterDown();
  static bool encoderButtonDown();
  static long encoderPosition();
  static int8_t lastEncoderDelta();
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
  static int8_t sPendingEncoderDelta;
};

}  // namespace Somnium
