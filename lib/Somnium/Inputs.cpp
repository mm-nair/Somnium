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
int8_t Inputs::sLastModeRequest = 0;

namespace {

struct PostedEvent {
  InputEvent event;
  int8_t extra;
};

constexpr int kPostedCap = 8;
PostedEvent sPosted[kPostedCap];
uint8_t sPostedHead = 0;
uint8_t sPostedTail = 0;
portMUX_TYPE sPostedMux = portMUX_INITIALIZER_UNLOCKED;

bool postEvent(InputEvent event, int8_t extra) {
  portENTER_CRITICAL(&sPostedMux);
  const uint8_t next = static_cast<uint8_t>((sPostedTail + 1) % kPostedCap);
  if (next == sPostedHead) {
    portEXIT_CRITICAL(&sPostedMux);
    return false;
  }
  sPosted[sPostedTail].event = event;
  sPosted[sPostedTail].extra = extra;
  sPostedTail = next;
  portEXIT_CRITICAL(&sPostedMux);
  return true;
}

bool takeEvent(PostedEvent &out) {
  portENTER_CRITICAL(&sPostedMux);
  if (sPostedHead == sPostedTail) {
    portEXIT_CRITICAL(&sPostedMux);
    return false;
  }
  out = sPosted[sPostedHead];
  sPostedHead = static_cast<uint8_t>((sPostedHead + 1) % kPostedCap);
  portEXIT_CRITICAL(&sPostedMux);
  return true;
}

}  // namespace

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

int8_t Inputs::lastModeRequest() { return sLastModeRequest; }

bool Inputs::postShutter() {
  // The UI acts on the release edge. One posted release is one shutter.
  return postEvent(InputEvent::ShutterReleased, 0);
}

bool Inputs::postMode(int8_t index) {
  return postEvent(InputEvent::ModeRequested, index);
}

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
  PostedEvent posted;
  if (takeEvent(posted)) {
    if (posted.event == InputEvent::ModeRequested) {
      sLastModeRequest = posted.extra;
    } else if (posted.event == InputEvent::EncoderTurned) {
      sLastEncoderDelta = posted.extra > 0 ? 1 : -1;
    }
    return posted.event;
  }
  return InputEvent::None;
}

}  // namespace Somnium
