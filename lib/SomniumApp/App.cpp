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
#include <math.h>
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
bool gProcessHolding = false;  // error state — wait for dial-click
bool gIgnoreNextEncRelease = false;
int gShotCount = 0;
uint8_t *gResultJpeg = nullptr;
size_t gResultLen = 0;
uint8_t *gStorageJpeg = nullptr;
size_t gStorageLen = 0;
char gStorageLabel[20] = "";

// In-RAM gallery so raw/mod pairs work even without an SD card.
struct GalItem {
  char label[20];
  uint8_t *data = nullptr;
  size_t len = 0;
};
constexpr int kGalMax = 24;
GalItem gGal[kGalMax];
int gGalCount = 0;

void freeResult() {
  free(gResultJpeg);
  gResultJpeg = nullptr;
  gResultLen = 0;
}

void freeStorageJpeg() {
  // Storage browser may point at SD-loaded OR gallery-owned buffers.
  // Only free if not an alias into gGal.
  bool owned = true;
  for (int i = 0; i < gGalCount; ++i) {
    if (gStorageJpeg == gGal[i].data) {
      owned = false;
      break;
    }
  }
  if (owned) {
    Somnium::Storage::freeJpeg(gStorageJpeg);
  }
  gStorageJpeg = nullptr;
  gStorageLen = 0;
}

char gLabelBuf[20] = "";
char gReviewTitle[28] = "";

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
  if (strstr(base, "anime") != nullptr) {
    return "ANIME";
  }
  return "PLATE";
}

const char *labelFromPath(const char *path) {
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

void galPush(const char *label, const uint8_t *data, size_t len) {
  if (data == nullptr || len == 0) {
    return;
  }
  uint8_t *copy = static_cast<uint8_t *>(ps_malloc(len));
  if (copy == nullptr) {
    copy = static_cast<uint8_t *>(malloc(len));
  }
  if (copy == nullptr) {
    return;
  }
  memcpy(copy, data, len);

  if (gGalCount >= kGalMax) {
    free(gGal[0].data);
    memmove(&gGal[0], &gGal[1], sizeof(GalItem) * (kGalMax - 1));
    gGalCount = kGalMax - 1;
    gGal[gGalCount] = GalItem{};
  }
  strncpy(gGal[gGalCount].label, label != nullptr ? label : "PLATE",
          sizeof(gGal[gGalCount].label) - 1);
  gGal[gGalCount].label[sizeof(gGal[gGalCount].label) - 1] = '\0';
  gGal[gGalCount].data = copy;
  gGal[gGalCount].len = len;
  gGalCount += 1;
}

void savePair(const char *styleId, int shot, const uint8_t *orig, size_t origLen,
              const uint8_t *dream, size_t dreamLen) {
  char path[72];
  char label[20];

  if (orig != nullptr && origLen > 0) {
    snprintf(label, sizeof(label), "%s·raw", styleToken(styleId));
    galPush(label, orig, origLen);
    if (Somnium::Storage::ready()) {
      snprintf(path, sizeof(path), "/somnium/%s_%d_0_orig.jpg", styleId, shot);
      Somnium::Storage::saveJpeg(path, orig, origLen);
    }
  }
  if (dream != nullptr && dreamLen > 0) {
    snprintf(label, sizeof(label), "%s·mod", styleToken(styleId));
    galPush(label, dream, dreamLen);
    if (Somnium::Storage::ready()) {
      snprintf(path, sizeof(path), "/somnium/%s_%d_1_dream.jpg", styleId, shot);
      Somnium::Storage::saveJpeg(path, dream, dreamLen);
    }
  }
  Somnium::Console::printf("GAL %d plates (sd=%d)\n", gGalCount,
                           Somnium::Storage::ready() ? 1 : 0);
}

int browseCount() {
  // Prefer in-RAM gallery (trusted copies from this session). SD is a backup
  // when RAM is empty (e.g. after a future persist-only path).
  if (gGalCount > 0) {
    return gGalCount;
  }
  return Somnium::Storage::photoCount();
}

bool loadStorageIndex(int index0) {
  freeStorageJpeg();
  gStorageLabel[0] = '\0';
  const int n = browseCount();
  if (n <= 0 || index0 < 0 || index0 >= n) {
    gPreview.setPhoto(nullptr, 0);
    gPreview.setMeta("", 0, n);
    return false;
  }

  if (gGalCount > 0) {
    // RAM gallery (raw/mod pairs from this boot).
    strncpy(gStorageLabel, gGal[index0].label, sizeof(gStorageLabel) - 1);
    gStorageLabel[sizeof(gStorageLabel) - 1] = '\0';
    gStorageJpeg = gGal[index0].data;
    gStorageLen = gGal[index0].len;
    gPreview.setPhoto(gStorageJpeg, gStorageLen);
    gPreview.setMeta(gStorageLabel, index0 + 1, n);
    return true;
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
  gProcessHolding = false;
  gProcess.clearFailed();
  Somnium::Ui::Ui::replace(&gMode, Somnium::Ui::Transition::None);
}

void showProcess() {
  gProcess.setModeLabel(gMode.selected().label);
  gProcess.setStatus("warming up…");
  gProcess.clearFailed();
  gScreen = Screen::Process;
  gAwaitProcessWork = true;
  gProcessHolding = false;
  Somnium::Ui::Ui::replace(&gProcess, Somnium::Ui::Transition::None);
}

void showReview() {
  gShotCount += 1;
  snprintf(gReviewTitle, sizeof(gReviewTitle), "%s · dream",
           gMode.selected().label);
  gPreview.beginReview(gReviewTitle, gResultJpeg, gResultLen);
  gScreen = Screen::Review;
  Somnium::Ui::Ui::replace(&gPreview, Somnium::Ui::Transition::None);
}

void pumpCookUi() {
  // Keep Process anim alive during long xAI cooks; don't re-enter mapInputs.
  Somnium::Ui::Ui::tick();
  yield();
}

void failProcess(const char *status) {
  Somnium::Console::printf("ERR process %s\n", status != nullptr ? status : "?");
  gProcess.setStatus(status);
  gAwaitProcessWork = false;
  gProcessHolding = true;
}

void showStorage() {
  freeStorageJpeg();
  const int n = browseCount();
  const int start = n > 0 ? n - 1 : 0;
  gPreview.beginStorage(start, n);
  loadStorageIndex(start);
  gScreen = Screen::Storage;
  Somnium::Ui::Ui::replace(&gPreview, Somnium::Ui::Transition::None);
}

void requestCapture() {
  // Shutter always means: leave current UI and shoot with the selected mode.
  if (gScreen == Screen::Storage) {
    freeStorageJpeg();
  }
  if (gScreen == Screen::Review) {
    freeResult();
  }
  if (gScreen == Screen::Process) {
    return;  // already cooking
  }
  showProcess();
}

void runPipeline() {
  Somnium::ApiClient::setPump(pumpCookUi);

  if (!gCameraOk) {
    failProcess("no camera — dial back");
    return;
  }

  gProcess.setStatus("wifi…");
  pumpCookUi();
  if (!Somnium::Wifi::isConnected()) {
    Somnium::Console::println("WIFI wait…");
    Somnium::Wifi::waitConnected(20000);
  }
  if (!Somnium::Wifi::isConnected()) {
    failProcess("no wifi — dial back");
    return;
  }

  gProcess.setStatus("server…");
  pumpCookUi();
  String healthErr;
  if (!Somnium::ApiClient::health(&healthErr)) {
    char buf[40];
    snprintf(buf, sizeof(buf), "no server (%s)", healthErr.c_str());
    failProcess(buf);
    return;
  }

  gProcess.setStatus("snap…");
  pumpCookUi();
  Somnium::JpegBuffer photo;
  if (!Somnium::Image::takePhoto(photo)) {
    failProcess("capture fail — dial back");
    return;
  }

  char takenAt[32] = {0};
  const bool haveTime = Somnium::TimeSync::formatUtc(takenAt, sizeof(takenAt));
  if (!haveTime) {
    Somnium::Console::println("TIME not synced yet — mood uses server clock");
  }

  Somnium::ProcessRequest req;
  req.jpeg = photo.data;
  req.jpegLength = photo.length;
  req.styleId = gMode.selected().id;
  req.takenAt = haveTime ? takenAt : nullptr;
  req.lat = ServerConfig::kVenueLat;
  req.lon = ServerConfig::kVenueLon;

  gProcess.setStatus("cooking on thinkpad…");
  pumpCookUi();
  Somnium::Console::printf("API %s style=%s t=%s\n", ServerConfig::kHost,
                           req.styleId, haveTime ? takenAt : "-");
  Somnium::ProcessResponse resp;
  const bool ok = Somnium::ApiClient::process(req, resp);
  if (!ok) {
    Somnium::Console::printf("ERR api %s\n", resp.error.c_str());
    char buf[40];
    snprintf(buf, sizeof(buf), "api %s", resp.error.c_str());
    buf[sizeof(buf) - 1] = '\0';
    Somnium::ApiClient::freeResponse(resp);
    Somnium::Image::freePhoto(photo);
    failProcess(buf);
    return;
  }

  Somnium::Console::printf("API ok %u B  %s\n",
                           static_cast<unsigned>(resp.length),
                           resp.context.c_str());
  // Dual slideshow on device; ThinkPad already has *_0_orig / *_1_dream.
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
        // Shutter is ONLY "take a picture" — never navigation.
        if (gScreen == Screen::Mode || gScreen == Screen::Storage ||
            gScreen == Screen::Review) {
          requestCapture();
        }
        break;

      case Somnium::InputEvent::EncoderPressed:
        // Dismiss stuck screens on press (don't wait for release).
        if (gScreen == Screen::Review) {
          freeResult();
          showMode();
          gIgnoreNextEncRelease = true;
        } else if (gScreen == Screen::Process && gProcessHolding) {
          showMode();
          gIgnoreNextEncRelease = true;
        }
        break;

      case Somnium::InputEvent::EncoderReleased:
        if (gIgnoreNextEncRelease) {
          gIgnoreNextEncRelease = false;
          break;
        }
        // Dial click: Modes ↔ Storage; also dismiss Review if press was missed.
        if (gScreen == Screen::Mode) {
          showStorage();
        } else if (gScreen == Screen::Storage) {
          freeStorageJpeg();
          showMode();
        } else if (gScreen == Screen::Review) {
          freeResult();
          showMode();
        } else if (gScreen == Screen::Process && gProcessHolding) {
          showMode();
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

  // Mode-page flags unused for shutter/storage now (handled in mapInputs),
  // but keep Select/Back wiring harmless if something else triggers them.
  if (gScreen == Screen::Mode) {
    if (gMode.takePhotoRequested()) {
      gMode.clearTakePhoto();
      requestCapture();
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
    if (gPreview.backRequested()) {
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
