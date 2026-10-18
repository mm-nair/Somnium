#include "Storage.h"

#include <FS.h>
#include <SD_MMC.h>
#include <string.h>

#include "Console.h"

namespace Somnium {
namespace {

bool gReady = false;
constexpr int kMaxPhotos = 64;
char gPaths[kMaxPhotos][64];
int gCount = 0;
bool gListed = false;

void clearList() {
  gCount = 0;
  gListed = false;
}

bool endsWithJpg(const char *name) {
  if (name == nullptr) {
    return false;
  }
  const size_t n = strlen(name);
  if (n < 4) {
    return false;
  }
  const char *ext = name + (n - 4);
  return strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".JPG") == 0;
}

void rescan() {
  gCount = 0;
  gListed = true;
  if (!gReady) {
    return;
  }
  File dir = SD_MMC.open("/somnium");
  if (!dir || !dir.isDirectory()) {
    return;
  }
  File entry = dir.openNextFile();
  while (entry && gCount < kMaxPhotos) {
    if (!entry.isDirectory()) {
      const char *name = entry.name();
      // SD_MMC may return "/somnium/foo.jpg" or just "foo.jpg"
      const char *base = name;
      if (const char *slash = strrchr(name, '/')) {
        base = slash + 1;
      }
      if (endsWithJpg(base)) {
        if (name[0] == '/') {
          strncpy(gPaths[gCount], name, sizeof(gPaths[gCount]) - 1);
        } else {
          snprintf(gPaths[gCount], sizeof(gPaths[gCount]), "/somnium/%s", base);
        }
        gPaths[gCount][sizeof(gPaths[gCount]) - 1] = '\0';
        gCount += 1;
      }
    }
    entry.close();
    entry = dir.openNextFile();
  }
  dir.close();
}

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
    clearList();
    return false;
  }
  gReady = true;
  SD_MMC.mkdir("/somnium");
  clearList();
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
  clearList();  // force rescan next browse
  return written == length;
}

int Storage::photoCount() {
  if (!gListed) {
    rescan();
  }
  return gCount;
}

bool Storage::photoPathAt(int index, char *out, size_t outLen) {
  if (out == nullptr || outLen == 0) {
    return false;
  }
  if (!gListed) {
    rescan();
  }
  if (index < 0 || index >= gCount) {
    return false;
  }
  strncpy(out, gPaths[index], outLen - 1);
  out[outLen - 1] = '\0';
  return true;
}

bool Storage::loadJpeg(const char *path, uint8_t **outData, size_t *outLen) {
  if (!gReady || path == nullptr || outData == nullptr || outLen == nullptr) {
    return false;
  }
  *outData = nullptr;
  *outLen = 0;
  File file = SD_MMC.open(path, FILE_READ);
  if (!file) {
    return false;
  }
  const size_t len = file.size();
  if (len == 0 || len > 400000) {
    file.close();
    return false;
  }
  uint8_t *buf = static_cast<uint8_t *>(malloc(len));
  if (buf == nullptr) {
    file.close();
    return false;
  }
  const size_t got = file.read(buf, len);
  file.close();
  if (got != len) {
    free(buf);
    return false;
  }
  *outData = buf;
  *outLen = len;
  return true;
}

void Storage::freeJpeg(uint8_t *data) { free(data); }

}  // namespace Somnium
