#include "Storage.h"

#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <stdlib.h>
#include <string.h>

#include "Config.h"
#include "Console.h"
#include "Image.h"
#include "Power.h"

namespace Somnium {
namespace {

bool gReady = false;
constexpr int kMaxPhotos = 64;
struct PhotoEntry {
  char path[64];
  time_t modified;
};
PhotoEntry gPhotos[kMaxPhotos];
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
  const auto &first = *static_cast<const PhotoEntry *>(a);
  const auto &second = *static_cast<const PhotoEntry *>(b);
  if (first.modified < second.modified) {
    return -1;
  }
  if (first.modified > second.modified) {
    return 1;
  }
  return strcmp(first.path, second.path);
}

void rescan() {
  gCount = 0;
  gListed = true;
  if (!gReady) {
    return;
  }
  File dir = SD.open("/somnium");
  if (!dir || !dir.isDirectory()) {
    return;
  }
  File entry = dir.openNextFile();
  while (entry) {
    if (!entry.isDirectory()) {
      const char *name = entry.name();
      const char *base = name;
      if (const char *slash = strrchr(name, '/')) {
        base = slash + 1;
      }
      if (endsWithJpg(base)) {
        PhotoEntry photo{};
        if (name[0] == '/') {
          strncpy(photo.path, name, sizeof(photo.path) - 1);
        } else {
          snprintf(photo.path, sizeof(photo.path), "/somnium/%s", base);
        }
        photo.modified = entry.getLastWrite();
        if (gCount < kMaxPhotos) {
          gPhotos[gCount++] = photo;
        } else {
          int oldest = 0;
          for (int i = 1; i < gCount; ++i) {
            if (pathCmp(&gPhotos[i], &gPhotos[oldest]) < 0) {
              oldest = i;
            }
          }
          if (pathCmp(&photo, &gPhotos[oldest]) > 0) {
            gPhotos[oldest] = photo;
          }
        }
      }
    }
    entry.close();
    entry = dir.openNextFile();
  }
  dir.close();
  if (gCount > 1) {
    qsort(gPhotos, static_cast<size_t>(gCount), sizeof(gPhotos[0]), pathCmp);
  }
}

}  // namespace

bool Storage::begin() {
  // Sense microSD shares SPI with the ST7789. Display::begin owns SPI.begin.
  pinMode(Pins::kSdCs, OUTPUT);
  digitalWrite(Pins::kSdCs, HIGH);
  if (!SD.begin(Pins::kSdCs, SPI, 10000000, "/sd", 5, false)) {
    Console::println("SD mount failed (ok if no card)");
    gReady = false;
    clearList();
    return false;
  }
  gReady = true;
  SD.mkdir("/somnium");
  SD.mkdir("/somnium/queue");
  clearList();
  Console::printf("SD ready (%d jpg)\n", photoCount());
  return true;
}

bool Storage::ready() { return gReady; }

int Storage::nextShotNumber(const char *styleId, int minimumShot) {
  int shot = minimumShot;
  if (!gReady) {
    return shot;
  }
  char originalPath[72];
  char dreamPath[72];
  while (true) {
    snprintf(originalPath, sizeof(originalPath), "/somnium/%s_%d_0_orig.jpg",
             styleId, shot);
    snprintf(dreamPath, sizeof(dreamPath), "/somnium/%s_%d_1_dream.jpg",
             styleId, shot);
    if (!SD.exists(originalPath) && !SD.exists(dreamPath)) {
      return shot;
    }
    ++shot;
  }
}

void Storage::clearGallery() {
  if (!gReady) {
    return;
  }
  // Collect paths first — removing while iterating SD dirs is flaky.
  char doomed[kMaxPhotos][64];
  int nDoomed = 0;
  File dir = SD.open("/somnium");
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
    if (SD.remove(doomed[i])) {
      removed += 1;
    }
  }
  clearList();
  Console::printf("SD wiped %d jpg\n", removed);
}

bool Storage::saveJpeg(const char *path, const uint8_t *data, size_t length) {
  PowerBusy busy;
  if (!gReady || path == nullptr || data == nullptr || length == 0) {
    return false;
  }
  // Critical: truncate/replace. FILE_WRITE alone can leave a longer old file's
  // tail attached → corrupted JPEG on read (rainbow noise on decode).
  SD.remove(path);
  File file = SD.open(path, FILE_WRITE);
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
    SD.remove(path);
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
  strncpy(out, gPhotos[index].path, outLen - 1);
  out[outLen - 1] = '\0';
  return true;
}

bool Storage::loadJpeg(const char *path, uint8_t **outData, size_t *outLen) {
  if (!gReady || path == nullptr || outData == nullptr || outLen == nullptr) {
    return false;
  }
  *outData = nullptr;
  *outLen = 0;
  File file = SD.open(path, FILE_READ);
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
  if (got != len || !Image::looksLikeJpeg(buf, len)) {
    free(buf);
    return false;
  }
  *outData = buf;
  *outLen = len;
  return true;
}

void Storage::freeJpeg(uint8_t *data) { free(data); }

}  // namespace Somnium
