#include "Inputs.h"

#include "Config.h"

namespace Somnium {

Inputs::Button Inputs::sShutter{};
Inputs::Button Inputs::sEncoderSw{};
uint8_t Inputs::sPrevEncoderState = 0;
int Inputs::sEncoderTransitions = 0;
long Inputs::sEncoderPosition = 0;
int8_t Inputs::sLastEncoderDelta = 0;
int8_t Inputs::sPendingEncoderDelta = 0;
unsigned long Inputs::sLastActivityMs = 0;
bool Inputs::sShutterHeld = false;
unsigned long Inputs::sShutterHeldAt = 0;
bool Inputs::sShutterLongSent = false;

void Inputs::noteActivity() { sLastActivityMs = millis(); }

unsigned long Inputs::lastActivityMs() { return sLastActivityMs; }

void Inputs::injectSelectClick() {
  sEncoderSw.pressEvent = true;
  sEncoderSw.releaseEvent = true;
  noteActivity();
}

void Inputs::settleEncoder() {
  sPrevEncoderState =
      (digitalRead(Pins::kEncoderClk) << 1) | digitalRead(Pins::kEncoderDt);
  sEncoderTransitions = 0;
}

void Inputs::initButton(Button &button, uint8_t pin) {
  button.pin = pin;
  pinMode(pin, INPUT_PULLUP);
  button.lastReading = digitalRead(pin);
  button.stableReading = button.lastReading;
  button.lastChangedAt = millis();
  button.pressEvent = false;
  button.releaseEvent = false;
}

void Inputs::updateButton(Button &button) {
  const int reading = digitalRead(button.pin);
  const unsigned long now = millis();
  if (reading != button.lastReading) {
    button.lastReading = reading;
    button.lastChangedAt = now;
  }
  if (reading != button.stableReading &&
      now - button.lastChangedAt >= Defaults::kButtonDebounceMs) {
    const int previous = button.stableReading;
    button.stableReading = reading;
    if (previous == HIGH && reading == LOW) {
      button.pressEvent = true;
      noteActivity();
    } else if (previous == LOW && reading == HIGH) {
      button.releaseEvent = true;
      noteActivity();
    }
  }
}

void Inputs::updateEncoder() {
  // KY-040 detents are a full 4-step gray-code cycle. Firing every 2 steps
  // made one physical click advance the carousel twice.
  constexpr int8_t kDirections[16] = {0,  -1, 1, 0, 1, 0, 0,  -1,
                                      -1, 0,  0, 1, 0, 1, -1, 0};
  const uint8_t state =
      (digitalRead(Pins::kEncoderClk) << 1) | digitalRead(Pins::kEncoderDt);
  if (state == sPrevEncoderState) {
    return;
  }
  int8_t step = 0;
  if ((state ^ sPrevEncoderState) == 3) {
    sEncoderTransitions = 0;
  } else {
    step = kDirections[(sPrevEncoderState << 2) | state];
    sEncoderTransitions += step;
  }
  sPrevEncoderState = state;

  if (sEncoderTransitions >= 4 || sEncoderTransitions <= -4) {
    const int8_t dir = sEncoderTransitions > 0 ? 1 : -1;
    sEncoderPosition += dir;
    sPendingEncoderDelta =
        static_cast<int8_t>(sPendingEncoderDelta + dir);
    sEncoderTransitions = 0;
    noteActivity();
  }
}

void Inputs::begin() {
  pinMode(Pins::kEncoderClk, INPUT_PULLUP);
  pinMode(Pins::kEncoderDt, INPUT_PULLUP);
  sPrevEncoderState =
      (digitalRead(Pins::kEncoderClk) << 1) | digitalRead(Pins::kEncoderDt);
  sEncoderTransitions = 0;
  sEncoderPosition = 0;
  sLastEncoderDelta = 0;
  sPendingEncoderDelta = 0;
  initButton(sShutter, Pins::kShutterButton);
  initButton(sEncoderSw, Pins::kSelectButton);
  sShutterHeld = false;
  sShutterLongSent = false;
  noteActivity();
}

void Inputs::update() {
  updateEncoder();
  updateButton(sShutter);
  updateButton(sEncoderSw);
  // A long shutter hold is the logical select when the encoder has no
  // switch and no standalone button is fitted on the same GPIO.
  const unsigned long now = millis();
  if (sShutter.stableReading == LOW) {
    if (!sShutterHeld) {
      sShutterHeld = true;
      sShutterHeldAt = now;
      sShutterLongSent = false;
    } else if (!sShutterLongSent && now - sShutterHeldAt >= Defaults::kSelectHoldMs) {
      sShutterLongSent = true;
      sEncoderSw.pressEvent = true;
      noteActivity();
    }
  } else if (sShutterHeld) {
    sShutterHeld = false;
    if (sShutterLongSent) {
      sEncoderSw.releaseEvent = true;
      sShutter.releaseEvent = false;
      sShutter.pressEvent = false;
      noteActivity();
    }
  }
}

bool Inputs::shutterDown() { return sShutter.stableReading == LOW; }

bool Inputs::encoderButtonDown() { return sEncoderSw.stableReading == LOW; }

long Inputs::encoderPosition() { return sEncoderPosition; }

int8_t Inputs::lastEncoderDelta() { return sLastEncoderDelta; }

InputEvent Inputs::pollEvent() {
  if (sShutter.pressEvent) {
    sShutter.pressEvent = false;
    return InputEvent::ShutterPressed;
  }
  if (sShutter.releaseEvent) {
    sShutter.releaseEvent = false;
    return InputEvent::ShutterReleased;
  }
  if (sEncoderSw.pressEvent) {
    sEncoderSw.pressEvent = false;
    return InputEvent::EncoderPressed;
  }
  if (sEncoderSw.releaseEvent) {
    sEncoderSw.releaseEvent = false;
    return InputEvent::EncoderReleased;
  }
  if (sPendingEncoderDelta != 0) {
    sLastEncoderDelta = sPendingEncoderDelta > 0 ? 1 : -1;
    sPendingEncoderDelta =
        static_cast<int8_t>(sPendingEncoderDelta - sLastEncoderDelta);
    return InputEvent::EncoderTurned;
  }
  return InputEvent::None;
}

}  // namespace Somnium
