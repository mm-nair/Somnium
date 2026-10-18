#include "App.h"

#include <Camera.h>
#include <Config.h>
#include <Console.h>
#include <Host.h>
#include <Image.h>
#include <Inputs.h>
#include <Storage.h>
#include <Ui.h>
#include <string.h>

#include "Brand.h"
#include "Pages.h"

namespace Somnium {
namespace App {
namespace {

enum class Screen : uint8_t { Boot, Mode, Menu, Process, Review, Storage };

BootPage gBoot;
ModePage gMode;
MenuPage gMenu;
ProcessPage gProcess;
PreviewPage gPreview;
Screen gScreen = Screen::Boot;
bool gCameraOk = false;
bool gAwaitProcessWork = false;
bool gProcessHolding = false;  // error state — wait for dial-click
int gShotCount = 0;
uint8_t *gResultJpeg = nullptr;
size_t gResultLen = 0;
uint8_t *gStorageJpeg = nullptr;
size_t gStorageLen = 0;
char gStorageLabel[24] = "";
uint8_t *gAltJpeg = nullptr;
size_t gAltLen = 0;
uint8_t *gOrigJpeg = nullptr;
size_t gOrigLen = 0;
constexpr uint32_t kLongPressMs = 450;
uint32_t gEncPressedMs = 0;

// In-RAM gallery so raw/mod pairs work even without an SD card.
struct GalItem {
  char styleId[16];
  int shot = 0;
  uint8_t kind = 1;
  char label[20];
  uint8_t *data = nullptr;
  size_t len = 0;
};
constexpr int kGalMax = 24;
GalItem gGal[kGalMax];
int gGalCount = 0;

// One plate is a shot: the original, the dream, or both.
struct Plate {
  char styleId[16];
  char label[20];
  int shot = 0;
  int rawIndex = -1;
  int dreamIndex = -1;
};
constexpr int kPlateMax = 96;
Plate gPlates[kPlateMax];
int gPlateCount = 0;

void freeResult() {
  free(gResultJpeg);
  gResultJpeg = nullptr;
  gResultLen = 0;
}

void freeOwnedJpeg(uint8_t *&jpeg) {
  if (jpeg == nullptr) {
    return;
  }
  bool owned = true;
  for (int i = 0; i < gGalCount; ++i) {
    if (jpeg == gGal[i].data) {
      owned = false;
      break;
    }
  }
  if (owned) {
    Somnium::Storage::freeJpeg(jpeg);
  }
  jpeg = nullptr;
}

void freeStorageJpeg() {
  // Storage browser may point at SD-loaded OR gallery-owned buffers.
  freeOwnedJpeg(gStorageJpeg);
  freeOwnedJpeg(gAltJpeg);
  gStorageLen = 0;
  gAltLen = 0;
}

void freeOrig() {
  free(gOrigJpeg);
  gOrigJpeg = nullptr;
  gOrigLen = 0;
}

char gLabelBuf[24] = "";
char gReviewTitle[28] = "";

bool parsePlateFile(const char *base, char *id, size_t idLen, int *shot, int *kind) {
  if (base == nullptr || id == nullptr || idLen == 0 || shot == nullptr || kind == nullptr) {
    return false;
  }
  const Mode *matched = nullptr;
  size_t matchedLen = 0;
  for (int i = 0; i < kModeCount; ++i) {
    const size_t n = strlen(kModes[i].id);
    if (strncmp(base, kModes[i].id, n) == 0 && base[n] == '_' && n > matchedLen) {
      matched = &kModes[i];
      matchedLen = n;
    }
  }
  if (matched == nullptr) {
    return false;
  }
  int which = 0;
  if (sscanf(base + matchedLen, "_%d_%d", shot, &which) != 2) {
    return false;
  }
  *kind = which == 0 ? 0 : 1;
  strncpy(id, matched->id, idLen - 1);
  id[idLen - 1] = '\0';
  return true;
}

void formatSideLabel(char *out, size_t outLen, const char *styleId, int kind) {
  snprintf(out, outLen, "%s-%s", labelForId(styleId), kind == 0 ? "raw" : "mod");
}

const char *labelFromPath(const char *path) {
  if (path == nullptr) {
    return "";
  }
  const char *base = strrchr(path, '/');
  base = base != nullptr ? base + 1 : path;
  char id[16];
  int shot = 0;
  int kind = 1;
  if (!parsePlateFile(base, id, sizeof(id), &shot, &kind)) {
    snprintf(gLabelBuf, sizeof(gLabelBuf), "PLATE");
    return gLabelBuf;
  }
  formatSideLabel(gLabelBuf, sizeof(gLabelBuf), id, kind);
  return gLabelBuf;
}

const char *friendlyCookError(const char *err) {
  if (err == nullptr || err[0] == '\0') {
    return "cook failed";
  }
  if (strcmp(err, "bridge timeout") == 0) {
    return "timed out - no reply";
  }
  if (strcmp(err, "dream crc mismatch") == 0) {
    return "checksum failed";
  }
  if (strcmp(err, "dream short") == 0) {
    return "dream cut off";
  }
  if (strcmp(err, "dream trailer") == 0) {
    return "trailer missing";
  }
  if (strcmp(err, "bad jpeg") == 0) {
    return "not a jpeg";
  }
  if (strcmp(err, "oom dream") == 0) {
    return "out of memory";
  }
  if (strcmp(err, "empty") == 0) {
    return "empty photo";
  }
  if (strcmp(err, "dream length") == 0) {
    return "dream too large";
  }
  if (strcmp(err, "bridge protocol mismatch") == 0) {
    return "protocol mismatch";
  }
  if (strcmp(err, "no camera - dial back") == 0) {
    return "camera not ready";
  }
  if (strcmp(err, "capture fail - dial back") == 0) {
    return "capture failed";
  }
  return err;
}

bool galPush(const char *styleId, int shot, int kind, const uint8_t *data, size_t len) {
  if (data == nullptr || len == 0) {
    return false;
  }
  uint8_t *copy = static_cast<uint8_t *>(ps_malloc(len));
  if (copy == nullptr) {
    copy = static_cast<uint8_t *>(malloc(len));
  }
  if (copy == nullptr) {
    return false;
  }
  memcpy(copy, data, len);

  if (gGalCount >= kGalMax) {
    free(gGal[0].data);
    memmove(&gGal[0], &gGal[1], sizeof(GalItem) * (kGalMax - 1));
    gGalCount = kGalMax - 1;
    gGal[gGalCount] = GalItem{};
  }
  GalItem &item = gGal[gGalCount];
  strncpy(item.styleId, styleId != nullptr ? styleId : "", sizeof(item.styleId) - 1);
  item.styleId[sizeof(item.styleId) - 1] = '\0';
  item.shot = shot;
  item.kind = kind == 0 ? 0 : 1;
  formatSideLabel(item.label, sizeof(item.label), item.styleId, item.kind);
  item.data = copy;
  item.len = len;
  gGalCount += 1;
  return true;
}

void savePair(const char *styleId, int shot, const uint8_t *orig, size_t origLen,
              const uint8_t *dream, size_t dreamLen) {
  char path[72];

  if (orig != nullptr && origLen > 0) {
    bool saved = false;
    if (Somnium::Storage::ready()) {
      snprintf(path, sizeof(path), "/somnium/%s_%d_0_orig.jpg", styleId, shot);
      saved = Somnium::Storage::saveJpeg(path, orig, origLen);
      if (!saved) {
        Somnium::Console::printf("ERR SD save %s\n", path);
      }
    }
    if (!saved && !galPush(styleId, shot, 0, orig, origLen)) {
      Somnium::Console::println("ERR gallery original not saved");
    }
  }
  if (dream != nullptr && dreamLen > 0) {
    bool saved = false;
    if (Somnium::Storage::ready()) {
      snprintf(path, sizeof(path), "/somnium/%s_%d_1_dream.jpg", styleId, shot);
      saved = Somnium::Storage::saveJpeg(path, dream, dreamLen);
      if (!saved) {
        Somnium::Console::printf("ERR SD save %s\n", path);
      }
    }
    if (!saved && !galPush(styleId, shot, 1, dream, dreamLen)) {
      Somnium::Console::println("ERR gallery dream not saved");
    }
  }
  const int savedCount = Somnium::Storage::photoCount();
  Somnium::Console::printf("GAL %d plates (sd=%d ram=%d)\n",
                           savedCount + gGalCount, savedCount, gGalCount);
}

int browseCount() {
  return Somnium::Storage::photoCount() + gGalCount;
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

  const int savedCount = Somnium::Storage::photoCount();
  if (index0 >= savedCount) {
    const int galleryIndex = index0 - savedCount;
    strncpy(gStorageLabel, gGal[galleryIndex].label, sizeof(gStorageLabel) - 1);
    gStorageLabel[sizeof(gStorageLabel) - 1] = '\0';
    gStorageJpeg = gGal[galleryIndex].data;
    gStorageLen = gGal[galleryIndex].len;
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

bool describeBrowse(int index, char *id, size_t idLen, int *shot, int *kind) {
  const int saved = Somnium::Storage::photoCount();
  if (index < 0 || index >= browseCount()) {
    return false;
  }
  if (index >= saved) {
    const GalItem &item = gGal[index - saved];
    if (item.styleId[0] == '\0') {
      return false;
    }
    strncpy(id, item.styleId, idLen - 1);
    id[idLen - 1] = '\0';
    *shot = item.shot;
    *kind = item.kind;
    return true;
  }
  char path[64];
  if (!Somnium::Storage::photoPathAt(index, path, sizeof(path))) {
    return false;
  }
  const char *base = strrchr(path, '/');
  base = base != nullptr ? base + 1 : path;
  return parsePlateFile(base, id, idLen, shot, kind);
}

void rebuildPlates() {
  gPlateCount = 0;
  const int n = browseCount();
  for (int i = 0; i < n && gPlateCount < kPlateMax; ++i) {
    char id[16];
    int shot = 0;
    int kind = 1;
    if (!describeBrowse(i, id, sizeof(id), &shot, &kind)) {
      Plate &plate = gPlates[gPlateCount];
      plate = Plate{};
      strncpy(plate.label, "PLATE", sizeof(plate.label) - 1);
      plate.shot = i + 1;
      plate.rawIndex = -1;
      plate.dreamIndex = i;
      gPlateCount += 1;
      continue;
    }
    Plate *plate = nullptr;
    for (int p = 0; p < gPlateCount; ++p) {
      if (gPlates[p].shot == shot && strcmp(gPlates[p].styleId, id) == 0) {
        plate = &gPlates[p];
        break;
      }
    }
    if (plate == nullptr) {
      plate = &gPlates[gPlateCount];
      *plate = Plate{};
      strncpy(plate->styleId, id, sizeof(plate->styleId) - 1);
      plate->styleId[sizeof(plate->styleId) - 1] = '\0';
      strncpy(plate->label, labelForId(id), sizeof(plate->label) - 1);
      plate->label[sizeof(plate->label) - 1] = '\0';
      plate->shot = shot;
      plate->rawIndex = -1;
      plate->dreamIndex = -1;
      gPlateCount += 1;
    }
    if (kind == 0) {
      plate->rawIndex = i;
    } else {
      plate->dreamIndex = i;
    }
  }
}

bool takeBrowse(int index, uint8_t **out, size_t *len) {
  if (out == nullptr || len == nullptr || index < 0) {
    return false;
  }
  const int saved = Somnium::Storage::photoCount();
  if (index >= saved) {
    const int galleryIndex = index - saved;
    if (galleryIndex < 0 || galleryIndex >= gGalCount) {
      return false;
    }
    *out = gGal[galleryIndex].data;
    *len = gGal[galleryIndex].len;
    return true;
  }
  char path[64];
  if (!Somnium::Storage::photoPathAt(index, path, sizeof(path))) {
    return false;
  }
  return Somnium::Storage::loadJpeg(path, out, len);
}

bool loadPlate(int plateIndex, bool preferRaw) {
  freeStorageJpeg();
  gStorageLabel[0] = '\0';
  if (plateIndex < 0 || plateIndex >= gPlateCount) {
    gPreview.setPhoto(nullptr, 0);
    gPreview.setMeta("", 0, gPlateCount);
    return false;
  }
  const Plate &plate = gPlates[plateIndex];
  if (plate.dreamIndex >= 0) {
    takeBrowse(plate.dreamIndex, &gStorageJpeg, &gStorageLen);
  }
  if (plate.rawIndex >= 0) {
    takeBrowse(plate.rawIndex, &gAltJpeg, &gAltLen);
  }
  const bool showRaw = preferRaw && gAltJpeg != nullptr && gAltLen > 0;
  if (plate.shot > 0 && plate.label[0] != '\0') {
    snprintf(gStorageLabel, sizeof(gStorageLabel), "%s #%d", plate.label, plate.shot);
  } else {
    strncpy(gStorageLabel, plate.label, sizeof(gStorageLabel) - 1);
    gStorageLabel[sizeof(gStorageLabel) - 1] = '\0';
  }
  gPreview.setPair(gStorageJpeg, gStorageLen, gAltJpeg, gAltLen, showRaw);
  gPreview.setMeta(gStorageLabel, plateIndex + 1, gPlateCount);
  return gStorageJpeg != nullptr || gAltJpeg != nullptr;
}

void showMode() {
  gScreen = Screen::Mode;
  gAwaitProcessWork = false;
  gProcessHolding = false;
  gProcess.clearFailed();
  Somnium::Ui::Ui::replace(&gMode, Somnium::Ui::Transition::None);
}

void showMenu() {
  rebuildPlates();
  gMenu.setContext(gMode.selected().label, gPlateCount);
  gScreen = Screen::Menu;
  Somnium::Ui::Ui::replace(&gMenu, Somnium::Ui::Transition::None);
}

void showProcess() {
  gProcess.setModeLabel(gMode.selected().label);
  gProcess.setStage(ProcessPage::CookStage::Capture);
  gProcess.setStatus("capturing the frame");
  gProcess.clearFailed();
  gScreen = Screen::Process;
  gAwaitProcessWork = true;
  gProcessHolding = false;
  Somnium::Ui::Ui::replace(&gProcess, Somnium::Ui::Transition::None);
}

void showReview() {
  snprintf(gReviewTitle, sizeof(gReviewTitle), "%s", gMode.selected().label);
  gPreview.beginReview(gReviewTitle, gResultJpeg, gResultLen);
  gPreview.setAlternate(gOrigJpeg, gOrigLen);
  gScreen = Screen::Review;
  Somnium::Ui::Ui::replace(&gPreview, Somnium::Ui::Transition::None);
}

void pumpCookUi() {
  // Keep Process anim alive during long xAI cooks; don't re-enter mapInputs.
  Somnium::Ui::Ui::tick();
  yield();
}

void onCookStage(const char *stage) {
  if (stage == nullptr) {
    return;
  }
  if (strcmp(stage, "send") == 0) {
    gProcess.setStage(ProcessPage::CookStage::Send);
    gProcess.setStatus("sending over USB");
  } else if (strcmp(stage, "style") == 0) {
    gProcess.setStage(ProcessPage::CookStage::Style);
    gProcess.setStatus("styling on the laptop");
  } else if (strcmp(stage, "receive") == 0) {
    gProcess.setStage(ProcessPage::CookStage::Receive);
    gProcess.setStatus("receiving the dream");
  } else {
    return;
  }
  // One frame for the new stage. The style wait keeps pumping on its own
  // cadence; this does not run while JPEG bytes are on the wire.
  pumpCookUi();
}

void failProcess(const char *status) {
  Somnium::Console::printf("ERR process %s\n", status != nullptr ? status : "?");
  gProcess.setStatus(friendlyCookError(status));
  gProcess.setFailed();
  Somnium::Host::setPump(nullptr);
  Somnium::Host::setProgress(nullptr);
  gAwaitProcessWork = false;
  gProcessHolding = true;
}

void showStorage() {
  freeStorageJpeg();
  rebuildPlates();
  const int n = gPlateCount;
  const int start = n > 0 ? n - 1 : 0;
  gPreview.beginStorage(start, n);
  if (n > 0) {
    loadPlate(start, false);
  } else {
    gPreview.setPhoto(nullptr, 0);
    gPreview.setMeta("", 0, 0);
  }
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
    freeOrig();
  }
  if (gScreen == Screen::Process) {
    return;  // already cooking
  }
  showProcess();
}

bool retainCopy(const uint8_t *src, size_t len) {
  freeOrig();
  if (src == nullptr || len == 0) {
    return false;
  }
  gOrigJpeg = static_cast<uint8_t *>(ps_malloc(len));
  if (gOrigJpeg == nullptr) {
    gOrigJpeg = static_cast<uint8_t *>(malloc(len));
  }
  if (gOrigJpeg == nullptr) {
    return false;
  }
  memcpy(gOrigJpeg, src, len);
  gOrigLen = len;
  return true;
}

void leaveReview() {
  freeResult();
  freeOrig();
  showMode();
}

void onDial(bool held) {
  if (gScreen == Screen::Mode) {
    showMenu();
    return;
  }
  if (gScreen == Screen::Menu) {
    if (held) {
      showMode();
      return;
    }
    if (gMenu.selected() == MenuPage::kGallery) {
      showStorage();
    } else if (gMenu.selected() == MenuPage::kShoot) {
      requestCapture();
    } else {
      showMode();
    }
    return;
  }
  if (gScreen == Screen::Review) {
    leaveReview();
    return;
  }
  if (gScreen == Screen::Process && gProcessHolding) {
    showMode();
    return;
  }
  if (gScreen == Screen::Storage) {
    // Short click flips the plate when both sides exist. Otherwise, and on a
    // hold, return to the menu. flip() is not called on a hold.
    if (!held && gPreview.flip()) {
      return;
    }
    freeStorageJpeg();
    showMenu();
  }
}

void runPipeline() {
  Somnium::Host::setPump(pumpCookUi);
  Somnium::Host::setProgress(onCookStage);

  if (!gCameraOk) {
    failProcess("no camera - dial back");
    return;
  }

  gProcess.setStage(ProcessPage::CookStage::Capture);
  gProcess.setStatus("capturing the frame");
  pumpCookUi();
  Somnium::JpegBuffer photo;
  if (!Somnium::Image::takePhoto(photo)) {
    failProcess("capture fail - dial back");
    return;
  }
  // Sensor draws hard — power it down for the long USB/xAI wait so the
  // CH340 doesn't fall off the USB bus (kernel error -71 / ACM gone).
  Somnium::Camera::end();

  Somnium::JpegBuffer dream{};
  String bridgeErr;
  if (!Somnium::Host::cook(photo.data, photo.length, gMode.selected().id,
                           nullptr, Somnium::Defaults::kVenueLat,
                           Somnium::Defaults::kVenueLon, dream, &bridgeErr)) {
    Somnium::Console::printf("ERR bridge %s\n", bridgeErr.c_str());
    Somnium::Image::freePhoto(photo);
    char buf[48];
    snprintf(buf, sizeof(buf), "%s", bridgeErr.c_str());
    failProcess(buf);
    return;
  }

  Somnium::Console::printf("USB cook ok %u B\n",
                           static_cast<unsigned>(dream.length));
  gShotCount = Somnium::Storage::nextShotNumber(gMode.selected().id,
                                              gShotCount + 1);
  savePair(gMode.selected().id, gShotCount, photo.data, photo.length,
           dream.data, dream.length);
  retainCopy(photo.data, photo.length);
  Somnium::Image::freePhoto(photo);

  freeResult();
  gResultJpeg = dream.data;
  gResultLen = dream.length;
  dream.data = nullptr;
  Somnium::Host::setPump(nullptr);
  Somnium::Host::setProgress(nullptr);
  // Next shutter re-inits the sensor via Camera::captureJpeg.
  showReview();
  Somnium::Console::println("UI review");
}

void mapInputs() {
  Somnium::InputEvent ev = Somnium::Inputs::pollEvent();
  while (ev != Somnium::InputEvent::None) {
    switch (ev) {
      case Somnium::InputEvent::EncoderTurned:
        if (gScreen == Screen::Mode) {
          gMode.nudge(Somnium::Inputs::lastEncoderDelta());
        } else if (gScreen == Screen::Menu) {
          gMenu.nudge(Somnium::Inputs::lastEncoderDelta());
        } else if (gScreen == Screen::Storage) {
          const int before = gPreview.storageIndex();
          gPreview.nudge(Somnium::Inputs::lastEncoderDelta());
          const int after = gPreview.storageIndex();
          if (after != before) {
            loadPlate(after, false);
          }
        } else if (gScreen == Screen::Review) {
          gPreview.flip();
        }
        break;

      case Somnium::InputEvent::ShutterReleased:
        // Shutter is ONLY "take a picture" — never navigation.
        if (gScreen == Screen::Mode || gScreen == Screen::Menu ||
            gScreen == Screen::Storage || gScreen == Screen::Review) {
          requestCapture();
        }
        break;

      case Somnium::InputEvent::EncoderPressed:
        gEncPressedMs = millis();
        break;

      case Somnium::InputEvent::EncoderReleased:
        onDial(millis() - gEncPressedMs >= kLongPressMs);
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
    if (gMode.openMenuRequested()) {
      gMode.clearOpenMenu();
      showMenu();
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
      leaveReview();
    }
  }

  if (gScreen == Screen::Storage) {
    if (gPreview.backRequested()) {
      gPreview.clearBack();
      freeStorageJpeg();
      showMenu();
    }
  }

  Somnium::Ui::Ui::tick();
}

}  // namespace App
}  // namespace Somnium
