#include "Inputs.h"

#include "Config.h"

namespace Somnium {

Inputs::Button Inputs::sShutter{};
Inputs::Button Inputs::sEncoderSw{};
uint8_t Inputs::sPrevEncoderState = 0;
int Inputs::sEncoderTransitions = 0;
long Inputs::sEncoderPosition = 0;
int8_t Inputs::sLastEncoderDelta = 0;
bool Inputs::sEncoderTurnEvent = false;

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
    } else if (previous == LOW && reading == HIGH) {
      button.releaseEvent = true;
    }
  }
}

void Inputs::updateEncoder() {
  constexpr int8_t kDirections[16] = {0,  -1, 1, 0, 1, 0, 0,  -1,
                                      -1, 0,  0, 1, 0, 1, -1, 0};
  const uint8_t state =
      (digitalRead(Pins::kEncoderClk) << 1) | digitalRead(Pins::kEncoderDt);
  if (state == sPrevEncoderState) {
    return;
  }
  if ((state ^ sPrevEncoderState) == 3) {
    sEncoderTransitions = 0;
  } else {
    sEncoderTransitions += kDirections[(sPrevEncoderState << 2) | state];
  }
  sPrevEncoderState = state;
  if (sEncoderTransitions >= 4 || sEncoderTransitions <= -4) {
    sLastEncoderDelta = sEncoderTransitions > 0 ? 1 : -1;
    sEncoderPosition += sLastEncoderDelta;
    sEncoderTransitions = 0;
    sEncoderTurnEvent = true;
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
  sEncoderTurnEvent = false;
  initButton(sShutter, Pins::kShutterButton);
  initButton(sEncoderSw, Pins::kEncoderSw);
}

void Inputs::update() {
  updateEncoder();
  updateButton(sShutter);
  updateButton(sEncoderSw);
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
  if (sEncoderTurnEvent) {
    sEncoderTurnEvent = false;
    return InputEvent::EncoderTurned;
  }
  return InputEvent::None;
}

}  // namespace Somnium
