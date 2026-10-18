#include "Storage.h"

#include <FS.h>
#include <SD_MMC.h>
#include <stdlib.h>
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

int pathCmp(const void *a, const void *b) {
  return strcmp(static_cast<const char *>(a), static_cast<const char *>(b));
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
  if (gCount > 1) {
    qsort(gPaths, static_cast<size_t>(gCount), sizeof(gPaths[0]), pathCmp);
  }
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
  // Keep plates across reboot/flash — pull demos with ./scripts/usb_pull_sd.sh.
  // Wipe on demand: serial `sdwipe`.
  clearList();
  Console::printf("SD ready (%d jpg)\n", photoCount());
  return true;
}

bool Storage::ready() { return gReady; }

void Storage::clearGallery() {
  if (!gReady) {
    return;
  }
  // Collect paths first — removing while iterating SD_MMC dirs is flaky.
  char doomed[kMaxPhotos][64];
  int nDoomed = 0;
  File dir = SD_MMC.open("/somnium");
  if (dir && dir.isDirectory()) {
    File entry = dir.openNextFile();
    while (entry && nDoomed < kMaxPhotos) {
      if (!entry.isDirectory()) {
        const char *name = entry.name();
        const char *base = name;
        if (const char *slash = strrchr(name, '/')) {
          base = slash + 1;
        }
        if (endsWithJpg(base)) {
          if (name[0] == '/') {
            strncpy(doomed[nDoomed], name, sizeof(doomed[nDoomed]) - 1);
          } else {
            snprintf(doomed[nDoomed], sizeof(doomed[nDoomed]), "/somnium/%s",
                     base);
          }
          doomed[nDoomed][sizeof(doomed[nDoomed]) - 1] = '\0';
          nDoomed += 1;
        }
      }
      entry.close();
      entry = dir.openNextFile();
    }
    dir.close();
  }
  int removed = 0;
  for (int i = 0; i < nDoomed; ++i) {
    if (SD_MMC.remove(doomed[i])) {
      removed += 1;
    }
  }
  clearList();
  Console::printf("SD wiped %d jpg\n", removed);
}

bool Storage::saveJpeg(const char *path, const uint8_t *data, size_t length) {
  if (!gReady || path == nullptr || data == nullptr || length == 0) {
    return false;
  }
  // Critical: truncate/replace. FILE_WRITE alone can leave a longer old file's
  // tail attached → corrupted JPEG on read (rainbow noise on decode).
  SD_MMC.remove(path);
  File file = SD_MMC.open(path, FILE_WRITE);
  if (!file) {
    return false;
  }
  const size_t written = file.write(data, length);
  file.flush();
  file.close();
  clearList();
  if (written != length) {
    Console::printf("SD short write %u/%u %s\n", static_cast<unsigned>(written),
                    static_cast<unsigned>(length), path);
    SD_MMC.remove(path);
    return false;
  }
  return true;
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
  if (len < 4 || len > 400000) {
    file.close();
    return false;
  }
  uint8_t *buf = static_cast<uint8_t *>(
      heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (buf == nullptr) {
    buf = static_cast<uint8_t *>(malloc(len));
  }
  if (buf == nullptr) {
    file.close();
    return false;
  }
  const size_t got = file.read(buf, len);
  file.close();
  if (got != len || buf[0] != 0xFF || buf[1] != 0xD8) {
    free(buf);
    return false;
  }
  *outData = buf;
  *outLen = len;
  return true;
}

void Storage::freeJpeg(uint8_t *data) { free(data); }

}  // namespace Somnium
