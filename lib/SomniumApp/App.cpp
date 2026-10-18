#include "App.h"

#include <ApiClient.h>
#include <Console.h>
#include <Host.h>
#include <Image.h>
#include <Inputs.h>
#include <ServerConfig.h>
#include <Storage.h>
#include <TimeSync.h>
#include <Ui.h>
#include <Wifi.h>
#include <string.h>

#include "Brand.h"
#include "Pages.h"

namespace Somnium {
namespace App {
namespace {

enum class Screen : uint8_t { Boot, Mode, Process, Review, Storage };

BootPage gBoot;
ModePage gMode;
ProcessPage gProcess;
PreviewPage gPreview;
Screen gScreen = Screen::Boot;
bool gCameraOk = false;
bool gAwaitProcessWork = false;
int gShotCount = 0;
uint8_t *gResultJpeg = nullptr;
size_t gResultLen = 0;
uint8_t *gStorageJpeg = nullptr;
size_t gStorageLen = 0;
char gStorageLabel[16] = "";

void freeResult() {
  free(gResultJpeg);
  gResultJpeg = nullptr;
  gResultLen = 0;
}

void freeStorageJpeg() {
  Somnium::Storage::freeJpeg(gStorageJpeg);
  gStorageJpeg = nullptr;
  gStorageLen = 0;
}

char gLabelBuf[20] = "";

const char *styleToken(const char *base) {
  if (strstr(base, "noir") != nullptr) {
    return "NOIR";
  }
  if (strstr(base, "cartoon") != nullptr) {
    return "CARTOON";
  }
  if (strstr(base, "cyber") != nullptr) {
    return "CYBER";
  }
  if (strstr(base, "water") != nullptr || strstr(base, "wash") != nullptr) {
    return "WASH";
  }
  if (strstr(base, "gourmet") != nullptr) {
    return "GOURMET";
  }
  return "PLATE";
}

const char *labelFromPath(const char *path) {
  // "..._noir_0_orig.jpg" → "NOIR·raw" ; "..._1_dream.jpg" → "NOIR·mod"
  if (path == nullptr) {
    return "";
  }
  const char *base = strrchr(path, '/');
  base = base != nullptr ? base + 1 : path;
  const char *style = styleToken(base);
  const char *kind = "plt";
  if (strstr(base, "_0_orig") != nullptr || strstr(base, "_orig") != nullptr) {
    kind = "raw";
  } else if (strstr(base, "_1_dream") != nullptr ||
             strstr(base, "_dream") != nullptr ||
             strstr(base, "_mod") != nullptr) {
    kind = "mod";
  }
  snprintf(gLabelBuf, sizeof(gLabelBuf), "%s·%s", style, kind);
  return gLabelBuf;
}

void savePair(const char *styleId, int shot, const uint8_t *orig, size_t origLen,
              const uint8_t *dream, size_t dreamLen) {
  if (!Somnium::Storage::ready()) {
    return;
  }
  char path[72];
  if (orig != nullptr && origLen > 0) {
    snprintf(path, sizeof(path), "/somnium/%s_%d_0_orig.jpg", styleId, shot);
    Somnium::Storage::saveJpeg(path, orig, origLen);
  }
  if (dream != nullptr && dreamLen > 0) {
    snprintf(path, sizeof(path), "/somnium/%s_%d_1_dream.jpg", styleId, shot);
    Somnium::Storage::saveJpeg(path, dream, dreamLen);
  }
}

bool loadStorageIndex(int index0) {
  freeStorageJpeg();
  gStorageLabel[0] = '\0';
  const int n = Somnium::Storage::photoCount();
  if (n <= 0 || index0 < 0 || index0 >= n) {
    gPreview.setPhoto(nullptr, 0);
    gPreview.setMeta("", 0, n);
    return false;
  }
  char path[64];
  if (!Somnium::Storage::photoPathAt(index0, path, sizeof(path))) {
    gPreview.setPhoto(nullptr, 0);
    gPreview.setMeta("", index0 + 1, n);
    return false;
  }
  strncpy(gStorageLabel, labelFromPath(path), sizeof(gStorageLabel) - 1);
  gStorageLabel[sizeof(gStorageLabel) - 1] = '\0';
  if (!Somnium::Storage::loadJpeg(path, &gStorageJpeg, &gStorageLen)) {
    gPreview.setPhoto(nullptr, 0);
    gPreview.setMeta(gStorageLabel, index0 + 1, n);
    return false;
  }
  gPreview.setPhoto(gStorageJpeg, gStorageLen);
  gPreview.setMeta(gStorageLabel, index0 + 1, n);
  return true;
}

void showMode() {
  gScreen = Screen::Mode;
  gAwaitProcessWork = false;
  Somnium::Ui::Ui::replace(&gMode, Somnium::Ui::Transition::None);
}

void showProcess() {
  gProcess.setModeLabel(gMode.selected().label);
  gScreen = Screen::Process;
  gAwaitProcessWork = true;
  Somnium::Ui::Ui::replace(&gProcess, Somnium::Ui::Transition::None);
}

void showReview() {
  gShotCount += 1;
  gPreview.beginReview(gMode.selected().label, gResultJpeg, gResultLen);
  gScreen = Screen::Review;
  Somnium::Ui::Ui::replace(&gPreview, Somnium::Ui::Transition::None);
}

void showStorage() {
  freeStorageJpeg();
  const int n = Somnium::Storage::photoCount();
  const int start = n > 0 ? n - 1 : 0;  // newest first
  gPreview.beginStorage(start, n);
  loadStorageIndex(start);
  gScreen = Screen::Storage;
  Somnium::Ui::Ui::replace(&gPreview, Somnium::Ui::Transition::None);
}

void runPipeline() {
  if (!gCameraOk) {
    Somnium::Console::println("ERR camera");
    showMode();
    return;
  }

  Somnium::JpegBuffer photo;
  if (!Somnium::Image::takePhoto(photo)) {
    Somnium::Console::println("ERR capture");
    showMode();
    return;
  }

#if SOMNIUM_USE_SERVER
  if (Somnium::Wifi::isConnected()) {
    char takenAt[32] = {0};
    const bool haveTime =
        Somnium::TimeSync::formatUtc(takenAt, sizeof(takenAt));
    Somnium::ProcessRequest req;
    req.jpeg = photo.data;
    req.jpegLength = photo.length;
    req.styleId = gMode.selected().id;
    req.takenAt = haveTime ? takenAt : nullptr;

    Somnium::ProcessResponse resp;
    const bool ok = Somnium::ApiClient::process(req, resp);
    if (!ok) {
      Somnium::Console::printf("ERR api %s\n", resp.error.c_str());
      Somnium::ApiClient::freeResponse(resp);
      Somnium::Image::freePhoto(photo);
      showMode();
      return;
    }

    // Dual slideshow entries: unmodified then modified (dial alternates).
    savePair(gMode.selected().id, gShotCount + 1, photo.data, photo.length,
             resp.jpeg, resp.length);
    Somnium::Image::freePhoto(photo);

    freeResult();
    gResultJpeg = resp.jpeg;
    gResultLen = resp.length;
    resp.jpeg = nullptr;
    resp.length = 0;
    Somnium::ApiClient::freeResponse(resp);
    showReview();
    return;
  }
#endif

  // Offline: only the capture (still saved as the "orig" slot).
  savePair(gMode.selected().id, gShotCount + 1, photo.data, photo.length,
           nullptr, 0);
  freeResult();
  gResultJpeg = photo.data;
  gResultLen = photo.length;
  photo.data = nullptr;
  photo.length = 0;
  Somnium::Image::freePhoto(photo);
  showReview();
}

void mapInputs() {
  Somnium::InputEvent ev = Somnium::Inputs::pollEvent();
  while (ev != Somnium::InputEvent::None) {
    switch (ev) {
      case Somnium::InputEvent::EncoderTurned:
        if (gScreen == Screen::Mode) {
          gMode.nudge(Somnium::Inputs::lastEncoderDelta());
        } else if (gScreen == Screen::Storage) {
          const int before = gPreview.storageIndex();
          gPreview.nudge(Somnium::Inputs::lastEncoderDelta());
          const int after = gPreview.storageIndex();
          if (after != before) {
            loadStorageIndex(after);
          }
        }
        break;
      case Somnium::InputEvent::ShutterReleased:
        if (gScreen == Screen::Mode || gScreen == Screen::Review ||
            gScreen == Screen::Storage) {
          Somnium::Ui::Ui::handleAction(Somnium::Ui::NavAction::Select);
        }
        break;
      case Somnium::InputEvent::EncoderReleased:
        if (gScreen == Screen::Mode || gScreen == Screen::Review ||
            gScreen == Screen::Storage) {
          Somnium::Ui::Ui::handleAction(Somnium::Ui::NavAction::Back);
        }
        break;
      default:
        break;
    }
    ev = Somnium::Inputs::pollEvent();
  }
}

}  // namespace

void Runtime::begin(bool cameraOk) {
  gCameraOk = cameraOk;
  Brand::applyTheme(Somnium::Ui::Ui::theme());
  Somnium::Ui::Ui::begin();
  Somnium::Ui::Ui::push(&gBoot, Somnium::Ui::Transition::None);
  gScreen = Screen::Boot;
}

void Runtime::tick() {
  Somnium::Wifi::poll();
  Somnium::Host::poll();
  Somnium::Inputs::update();
  mapInputs();

  if (gScreen == Screen::Boot && gBoot.finished() &&
      !Somnium::Ui::Ui::navigator().busy()) {
    showMode();
  }

  if (gScreen == Screen::Mode) {
    if (gMode.takePhotoRequested()) {
      gMode.clearTakePhoto();
      showProcess();
    }
    if (gMode.openStorageRequested()) {
      gMode.clearOpenStorage();
      showStorage();
    }
  }

  if (gScreen == Screen::Process && gAwaitProcessWork &&
      gProcess.readyToWork() && !Somnium::Ui::Ui::navigator().busy()) {
    gAwaitProcessWork = false;
    Somnium::Ui::Ui::tick();
    runPipeline();
  }

  if (gScreen == Screen::Review) {
    if (gPreview.autoDone() || gPreview.backRequested()) {
      gPreview.clearBack();
      freeResult();
      showMode();
    }
  }

  if (gScreen == Screen::Storage) {
    if (gPreview.backRequested()) {
      gPreview.clearBack();
      freeStorageJpeg();
      showMode();
    }
  }

  Somnium::Ui::Ui::tick();
}

}  // namespace App
}  // namespace Somnium
