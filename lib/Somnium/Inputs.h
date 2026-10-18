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
  // Phone / other tasks. Drained by pollEvent after the physical edges.
  ModeRequested,
};

class Inputs {
 public:
  static void begin();
  static void update();

  static bool shutterDown();
  static bool encoderButtonDown();
  static long encoderPosition();
  static int8_t lastEncoderDelta();
  static int8_t lastModeRequest();
  static InputEvent pollEvent();

  // Safe to call from another task (the BLE host task). The UI loop is
  // still the only consumer, via pollEvent.
  static bool postShutter();
  static bool postMode(int8_t index);

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
  static int8_t sLastModeRequest;
};

}  // namespace Somnium
