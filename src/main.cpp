#include <Arduino.h>

#include <Somnium.h>
#include <SomniumUi.h>

#include <ServerConfig.h>

namespace {

bool gCameraOk = false;
int gStyleIndex = 0;

constexpr const char *kStyles[] = {"cartoon", "cyberpunk", "noir", "watercolor"};
constexpr int kStyleCount = 4;

const char *styleId() {
  int index = gStyleIndex % kStyleCount;
  if (index < 0) {
    index += kStyleCount;
  }
  return kStyles[index];
}

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
      gStyleIndex += Somnium::Inputs::lastEncoderDelta();
      Somnium::Console::printf("EVT encoder turn style=%s\n", styleId());
      break;
    default:
      break;
  }
}

void captureLocalOnly() {
  Somnium::JpegBuffer photo;
  if (!Somnium::Image::takePhoto(photo)) {
    Somnium::Console::println("ERR capture");
    return;
  }
  Somnium::Console::printf("SHOT local %u\n",
                           static_cast<unsigned>(photo.length));
  Somnium::Image::freePhoto(photo);
}

#if SOMNIUM_USE_SERVER
void captureAndProcess() {
  if (!gCameraOk) {
    Somnium::Console::println("ERR camera");
    return;
  }
  if (!Somnium::Wifi::isConnected()) {
    Somnium::Console::println("ERR wifi — falling back local");
    captureLocalOnly();
    return;
  }

  Somnium::JpegBuffer photo;
  if (!Somnium::Image::takePhoto(photo)) {
    Somnium::Console::println("ERR capture");
    return;
  }

  char takenAt[32] = {0};
  const bool haveTime = Somnium::TimeSync::formatUtc(takenAt, sizeof(takenAt));

  Somnium::ProcessRequest req;
  req.jpeg = photo.data;
  req.jpegLength = photo.length;
  req.styleId = styleId();
  req.takenAt = haveTime ? takenAt : nullptr;

  Somnium::Console::printf("PROCESS style=%s\n", req.styleId);
  Somnium::ProcessResponse resp;
  const bool ok = Somnium::ApiClient::process(req, resp);
  Somnium::Image::freePhoto(photo);

  if (!ok) {
    Somnium::Console::printf("ERR api %s\n", resp.error.c_str());
    return;
  }

  Somnium::Console::printf("OK api %u B\n", static_cast<unsigned>(resp.length));
  if (!Somnium::Image::drawJpeg(resp.jpeg, resp.length)) {
    Somnium::Console::println("WARN drawJpeg failed — LCD kept last frame");
  }

  if (Somnium::Storage::ready()) {
    char path[64];
    snprintf(path, sizeof(path), "/somnium/%s_%s.jpg",
             haveTime ? takenAt : "shot", req.styleId);
    for (char *p = path; *p; ++p) {
      if (*p == ':') {
        *p = '-';
      }
    }
    if (Somnium::Storage::saveJpeg(path, resp.jpeg, resp.length)) {
      Somnium::Console::printf("SD %s\n", path);
    } else {
      Somnium::Console::println("WARN sd save failed");
    }
  }

  Somnium::ApiClient::freeResponse(resp);
}
#endif

void onShutter() {
#if SOMNIUM_USE_SERVER
  captureAndProcess();
#else
  if (!gCameraOk) {
    Somnium::Console::println("ERR camera");
    return;
  }
  captureLocalOnly();
#endif
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
      onShutter();
    }
    event = Somnium::Inputs::pollEvent();
  }

  delayMicroseconds(100);
}
