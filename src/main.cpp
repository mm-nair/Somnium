#include <Arduino.h>

#include <Somnium.h>

// Bring-up firmware: pins, Wi-Fi, serial, shutter capture → LCD.
// No UI chrome and no backend API — those layers come later on top of Somnium::*.

namespace {

bool gCameraOk = false;
int gStyleIndex = 0;  // encoder cycles this for later API style_id wiring

const char *styleName(int index) {
  static const char *kStyles[] = {"ghibli", "cyberpunk", "noir", "watercolor"};
  constexpr int kCount = 4;
  if (index < 0) {
    index = (index % kCount + kCount) % kCount;
  }
  return kStyles[index % kCount];
}

}  // namespace

void setup() {
  gCameraOk = Somnium::begin();
  if (!gCameraOk) {
    Somnium::Console::println("Setup: camera failed — inputs/LCD still active.");
  } else {
    Somnium::Console::println(
        "Setup: press shutter (GPIO3) to capture; turn encoder to cycle style.");
  }
}

void loop() {
  Somnium::Wifi::poll();
  Somnium::Inputs::update();

  Somnium::InputEvent event = Somnium::Inputs::pollEvent();
  while (event != Somnium::InputEvent::None) {
    switch (event) {
      case Somnium::InputEvent::ShutterReleased:
        if (gCameraOk) {
          Somnium::JpegBuffer photo;
          if (Somnium::Image::takePhoto(photo)) {
            // Frame already drawn via Camera → Display callback.
            // photo.data is ready for a future HTTP upload.
            Somnium::Console::printf("Shutter: style=%s jpeg=%u B\n",
                                     styleName(gStyleIndex),
                                     static_cast<unsigned>(photo.length));
            Somnium::Image::freePhoto(photo);
          }
        } else {
          Somnium::Console::println("Shutter: camera not ready");
        }
        break;

      case Somnium::InputEvent::EncoderTurned:
        gStyleIndex += Somnium::Inputs::lastEncoderDelta();
        Somnium::Console::printf("Style: %s (index=%d pos=%ld)\n",
                                 styleName(gStyleIndex), gStyleIndex,
                                 Somnium::Inputs::encoderPosition());
        break;

      case Somnium::InputEvent::EncoderPressed:
        Somnium::Console::println("Encoder: pressed");
        break;

      case Somnium::InputEvent::EncoderReleased:
        Somnium::Console::println("Encoder: released");
        break;

      case Somnium::InputEvent::ShutterPressed:
        Somnium::Console::println("Shutter: pressed");
        break;

      default:
        break;
    }
    event = Somnium::Inputs::pollEvent();
  }

  delayMicroseconds(100);
}
