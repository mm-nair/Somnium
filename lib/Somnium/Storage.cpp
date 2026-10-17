#include "Storage.h"

#include <FS.h>
#include <SD_MMC.h>

#include "Console.h"

namespace Somnium {
namespace {

bool gReady = false;

}  // namespace

bool Storage::begin() {
  // Freenove FNK0085 SDMMC 1-bit: CLK=39 CMD=38 D0=40
  if (!SD_MMC.setPins(39, 38, 40)) {
    Console::println("SD pin setup failed");
    return false;
  }
  if (!SD_MMC.begin("/sd", true)) {
    Console::println("SD mount failed (ok if no card)");
    gReady = false;
    return false;
  }
  gReady = true;
  SD_MMC.mkdir("/somnium");
  Console::println("SD ready");
  return true;
}

bool Storage::ready() { return gReady; }

bool Storage::saveJpeg(const char *path, const uint8_t *data, size_t length) {
  if (!gReady || path == nullptr || data == nullptr || length == 0) {
    return false;
  }
  File file = SD_MMC.open(path, FILE_WRITE);
  if (!file) {
    return false;
  }
  const size_t written = file.write(data, length);
  file.close();
  return written == length;
}

}  // namespace Somnium
