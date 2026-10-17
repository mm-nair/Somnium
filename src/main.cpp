#include <Arduino.h>

#include <Somnium.h>
#include <SomniumUi.h>

namespace {

bool gCameraOk = false;

void emitEvent(Somnium::InputEvent event) {
  switch (event) {
    case Somnium::InputEvent::ShutterPressed:
      Somnium::Console::println("EVT shutter press");
      break;
    case Somnium::InputEvent::ShutterReleased:
      Somnium::Console::println("EVT shutter release");
      break;
    case Somnium::InputEvent::EncoderPressed:
      Somnium::Console::println("EVT encoder press");
      break;
    case Somnium::InputEvent::EncoderReleased:
      Somnium::Console::println("EVT encoder release");
      break;
    case Somnium::InputEvent::EncoderTurned:
      Somnium::Console::printf("EVT encoder turn delta=%d pos=%ld\n",
                               Somnium::Inputs::lastEncoderDelta(),
                               Somnium::Inputs::encoderPosition());
      break;
    default:
      break;
  }
}

void captureToLcd() {
  if (!gCameraOk) {
    Somnium::Console::println("ERR camera");
    return;
  }
  Somnium::JpegBuffer photo;
  if (!Somnium::Image::takePhoto(photo)) {
    Somnium::Console::println("ERR capture");
    return;
  }
  Somnium::Console::printf("SHOT %u %dx%d\n",
                           static_cast<unsigned>(photo.length), photo.width,
                           photo.height);
  Somnium::Image::freePhoto(photo);
}

}  // namespace

void setup() { gCameraOk = Somnium::begin(); }

void loop() {
  Somnium::Wifi::poll();
  Somnium::Host::poll();
  Somnium::Inputs::update();

  Somnium::InputEvent event = Somnium::Inputs::pollEvent();
  while (event != Somnium::InputEvent::None) {
    emitEvent(event);
    if (event == Somnium::InputEvent::ShutterReleased) {
      captureToLcd();
    }
    event = Somnium::Inputs::pollEvent();
  }

  delayMicroseconds(100);
}
