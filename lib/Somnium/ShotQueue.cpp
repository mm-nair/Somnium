#include "ShotQueue.h"

#include <FS.h>
#include <LittleFS.h>
#include <SD_MMC.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "Config.h"
#include "Console.h"
#include "Storage.h"

namespace Somnium {
namespace {

constexpr int kMaxTracked = 64;

struct Item {
  uint32_t sequence;
  size_t jpegLength;
  bool onFlash;
};

Item gItems[kMaxTracked];
int gCount = 0;
bool gFlash = false;
bool gSawExtra = false;
ShotQueue::Listener gListener = nullptr;
Preferences gPrefs;
bool gPrefsOpen = false;
uint32_t gLastSeq = 0;
uint32_t gFallbackSeq = 1;

fs::FS &medium(bool onFlash) {
  return onFlash ? static_cast<fs::FS &>(LittleFS) : static_cast<fs::FS &>(SD_MMC);
}

bool parseSeq(const char *name, uint32_t *seq) {
  if (name == nullptr || seq == nullptr) {
    return false;
  }
  const char *base = name;
  if (const char *slash = strrchr(name, '/')) {
    base = slash + 1;
  }
  unsigned long value = 0;
  char tail = '\0';
  if (sscanf(base, "q%08lu.jpg%c", &value, &tail) != 1 || value == 0) {
    return false;
  }
  *seq = static_cast<uint32_t>(value);
  return true;
}

void pathFor(uint32_t seq, const char *ext, char *out, size_t outLen) {
  snprintf(out, outLen, "/queue/q%08lu.%s", static_cast<unsigned long>(seq), ext);
}

bool tracked(uint32_t seq) {
  for (int i = 0; i < gCount; ++i) {
    if (gItems[i].sequence == seq) {
      return true;
    }
  }
  return false;
}

int indexOf(uint32_t seq) {
  for (int i = 0; i < gCount; ++i) {
    if (gItems[i].sequence == seq) {
      return i;
    }
  }
  return -1;
}

int oldestIndex() {
  int best = 0;
  for (int i = 1; i < gCount; ++i) {
    if (gItems[i].sequence < gItems[best].sequence) {
      best = i;
    }
  }
  return best;
}

int newestIndex() {
  int best = 0;
  for (int i = 1; i < gCount; ++i) {
    if (gItems[i].sequence > gItems[best].sequence) {
      best = i;
    }
  }
  return best;
}

void remember(uint32_t seq, size_t bytes, bool onFlash) {
  if (tracked(seq)) {
    return;
  }
  if (gCount < kMaxTracked) {
    gItems[gCount].sequence = seq;
    gItems[gCount].jpegLength = bytes;
    gItems[gCount].onFlash = onFlash;
    gCount += 1;
    return;
  }
  gSawExtra = true;
  const int newest = newestIndex();
  if (seq < gItems[newest].sequence) {
    gItems[newest].sequence = seq;
    gItems[newest].jpegLength = bytes;
    gItems[newest].onFlash = onFlash;
  }
}

void rescanOne(fs::FS &fs, bool onFlash) {
  File dir = fs.open("/queue");
  if (!dir || !dir.isDirectory()) {
    return;
  }
  File entry = dir.openNextFile();
  while (entry) {
    uint32_t seq = 0;
    if (!entry.isDirectory() && parseSeq(entry.name(), &seq)) {
      remember(seq, entry.size(), onFlash);
    }
    entry.close();
    entry = dir.openNextFile();
  }
  dir.close();
}

void rescan() {
  gCount = 0;
  gSawExtra = false;
  if (Storage::ready()) {
    rescanOne(SD_MMC, false);
  }
  if (gFlash) {
    rescanOne(LittleFS, true);
  }
}

bool removeFiles(fs::FS &fs, uint32_t seq) {
  char jpg[40];
  char meta[40];
  pathFor(seq, "jpg", jpg, sizeof(jpg));
  pathFor(seq, "meta", meta, sizeof(meta));
  const bool had = fs.exists(jpg) || fs.exists(meta);
  fs.remove(jpg);
  fs.remove(meta);
  return had;
}

void sweepUntracked(fs::FS &fs) {
  bool removed = true;
  while (removed) {
    removed = false;
    char doomed[8][40];
    int nDoomed = 0;
    File dir = fs.open("/queue");
    if (!dir) {
      return;
    }
    File entry = dir.openNextFile();
    while (entry && nDoomed < 8) {
      uint32_t seq = 0;
      if (!entry.isDirectory() && parseSeq(entry.name(), &seq) && !tracked(seq)) {
        pathFor(seq, "jpg", doomed[nDoomed], sizeof(doomed[nDoomed]));
        nDoomed += 1;
      }
      entry.close();
      entry = dir.openNextFile();
    }
    dir.close();
    for (int i = 0; i < nDoomed; ++i) {
      char meta[40];
      fs.remove(doomed[i]);
      // jpg path → meta path
      strncpy(meta, doomed[i], sizeof(meta) - 1);
      meta[sizeof(meta) - 1] = '\0';
      char *dot = strrchr(meta, '.');
      if (dot != nullptr) {
        snprintf(dot, sizeof(meta) - static_cast<size_t>(dot - meta), ".meta");
        fs.remove(meta);
      }
      removed = true;
    }
  }
}

bool readMeta(fs::FS &fs, uint32_t seq, bool onFlash, QueuedShot &out) {
  clearQueuedShot(out);
  out.sequence = seq;
  out.onFlash = onFlash;
  char path[40];
  pathFor(seq, "meta", path, sizeof(path));
  File file = fs.open(path, FILE_READ);
  if (!file) {
    return false;
  }
  while (file.available()) {
    char line[80];
    const int n = file.readBytesUntil('\n', line, sizeof(line) - 1);
    if (n <= 0) {
      continue;
    }
    line[n] = '\0';
    if (n > 0 && line[n - 1] == '\r') {
      line[n - 1] = '\0';
    }
    char *eq = strchr(line, '=');
    if (eq == nullptr) {
      continue;
    }
    *eq = '\0';
    const char *value = eq + 1;
    if (strcmp(line, "style") == 0) {
      strncpy(out.style, value, sizeof(out.style) - 1);
    } else if (strcmp(line, "taken_at") == 0) {
      strncpy(out.takenAt, value, sizeof(out.takenAt) - 1);
    } else if (strcmp(line, "bat_pct10") == 0) {
      out.batteryPctTimes10 = atoi(value);
    } else if (strcmp(line, "bat_mv") == 0) {
      out.batteryMv = atoi(value);
    } else if (strcmp(line, "chg") == 0) {
      out.charging = atoi(value);
    } else if (strcmp(line, "bytes") == 0) {
      out.jpegLength = static_cast<size_t>(strtoul(value, nullptr, 10));
    }
  }
  file.close();
  return out.style[0] != '\0';
}

bool writeAll(fs::FS &fs, const char *path, const uint8_t *data, size_t length) {
  fs.remove(path);
  File file = fs.open(path, FILE_WRITE);
  if (!file) {
    return false;
  }
  const size_t written = file.write(data, length);
  file.flush();
  file.close();
  if (written != length) {
    fs.remove(path);
    return false;
  }
  return true;
}

bool writeMeta(fs::FS &fs, const QueuedShot &meta, size_t length) {
  char path[40];
  pathFor(meta.sequence, "meta", path, sizeof(path));
  char body[160];
  snprintf(body, sizeof(body),
           "style=%s\ntaken_at=%s\nbat_pct10=%d\nbat_mv=%d\nchg=%d\nbytes=%u\n",
           meta.style, meta.takenAt, meta.batteryPctTimes10, meta.batteryMv,
           meta.charging, static_cast<unsigned>(length));
  return writeAll(fs, path, reinterpret_cast<const uint8_t *>(body), strlen(body));
}

size_t bytesOn(bool onFlash) {
  size_t total = 0;
  for (int i = 0; i < gCount; ++i) {
    if (gItems[i].onFlash == onFlash) {
      total += gItems[i].jpegLength;
    }
  }
  return total;
}

void openPrefs() {
  if (!gPrefsOpen) {
    gPrefsOpen = gPrefs.begin("somnium", false);
  }
}

bool evictOldest() {
  if (gCount <= 0) {
    return false;
  }
  const int index = oldestIndex();
  const uint32_t seq = gItems[index].sequence;
  const bool onFlash = gItems[index].onFlash;
  removeFiles(medium(onFlash), seq);
  // The same id should not live on both media.
  if (gFlash && Storage::ready()) {
    removeFiles(medium(!onFlash), seq);
  }
  rescan();
  if (gListener != nullptr) {
    gListener(ShotQueue::Event::Evicted, seq, gCount);
  }
  return true;
}

bool roomFor(bool onFlash, size_t length) {
  const size_t budget =
      onFlash ? Defaults::kQueueMaxFlashBytes : Defaults::kQueueMaxSdBytes;
  if (length > budget || Defaults::kQueueMaxShots <= 0) {
    return false;
  }
  uint32_t stalled = 0;
  while (gCount >= Defaults::kQueueMaxShots || bytesOn(onFlash) + length > budget) {
    if (gCount <= 0) {
      return false;
    }
    const uint32_t seq = gItems[oldestIndex()].sequence;
    if (seq == stalled) {
      return false;
    }
    stalled = seq;
    if (!evictOldest()) {
      return false;
    }
  }
  return true;
}

bool put(bool onFlash, const QueuedShot &meta, const uint8_t *jpeg, size_t length) {
  if (!roomFor(onFlash, length)) {
    return false;
  }
  fs::FS &fs = medium(onFlash);
  fs.mkdir("/queue");
  char jpg[40];
  pathFor(meta.sequence, "jpg", jpg, sizeof(jpg));
  if (!writeAll(fs, jpg, jpeg, length)) {
    return false;
  }
  if (!writeMeta(fs, meta, length)) {
    fs.remove(jpg);
    return false;
  }
  rescan();
  return true;
}

}  // namespace

bool ShotQueue::begin() {
  gCount = 0;
  gFlash = false;
  // The default 16 MB table's "spiffs" partition is otherwise unused.
  // format-on-fail turns it into LittleFS the first time SD is absent.
  Console::println("QUEUE flash mount");
  gFlash = LittleFS.begin(true, "/lfs", 10, "spiffs");
  if (gFlash) {
    LittleFS.mkdir("/queue");
  } else {
    Console::println("QUEUE flash unavailable");
  }
  if (Storage::ready()) {
    SD_MMC.mkdir("/queue");
  }
  rescan();
  if (gSawExtra) {
    if (Storage::ready()) {
      sweepUntracked(SD_MMC);
    }
    if (gFlash) {
      sweepUntracked(LittleFS);
    }
    rescan();
  }
  while (gCount > Defaults::kQueueMaxShots) {
    if (!evictOldest()) {
      break;
    }
  }
  openPrefs();
  if (gPrefsOpen) {
    uint32_t next = gPrefs.getUInt("nextSeq", 1);
    if (next == 0) {
      next = 1;
    }
    gFallbackSeq = next;
    gLastSeq = next > 1 ? next - 1 : 0;
  }
  Console::printf("QUEUE sd=%d flash=%d depth=%d next=%lu\n",
                  Storage::ready() ? 1 : 0, gFlash ? 1 : 0, gCount,
                  static_cast<unsigned long>(gFallbackSeq));
  return Storage::ready() || gFlash;
}

void ShotQueue::setListener(Listener listener) { gListener = listener; }

void ShotQueue::notify(Event event, uint32_t sequence) {
  if (gListener != nullptr) {
    gListener(event, sequence, gCount);
  }
}

uint32_t ShotQueue::allocateSequence() {
  openPrefs();
  uint32_t seq = gFallbackSeq;
  if (gPrefsOpen) {
    seq = gPrefs.getUInt("nextSeq", 1);
    if (seq == 0) {
      seq = 1;
    }
    gPrefs.putUInt("nextSeq", seq + 1);
  }
  gFallbackSeq = seq + 1;
  gLastSeq = seq;
  return seq;
}

uint32_t ShotQueue::lastSequence() { return gLastSeq; }

int ShotQueue::depth() { return gCount; }

bool ShotQueue::enqueue(const QueuedShot &meta, const uint8_t *jpeg, size_t length) {
  if (meta.sequence == 0 || meta.style[0] == '\0' || jpeg == nullptr || length < 4 ||
      length > 400000) {
    return false;
  }
  if (jpeg[0] != 0xff || jpeg[1] != 0xd8) {
    return false;
  }
  rescan();
  if (tracked(meta.sequence)) {
    return true;
  }
  bool stored = false;
  if (Storage::ready()) {
    stored = put(false, meta, jpeg, length);
  }
  if (!stored && gFlash) {
    stored = put(true, meta, jpeg, length);
  }
  if (!stored) {
    return false;
  }
  notify(Event::Enqueued, meta.sequence);
  Console::printf("QUEUE save seq=%lu bytes=%u flash=%d depth=%d\n",
                  static_cast<unsigned long>(meta.sequence),
                  static_cast<unsigned>(length),
                  indexOf(meta.sequence) >= 0 &&
                          gItems[indexOf(meta.sequence)].onFlash
                      ? 1
                      : 0,
                  gCount);
  return true;
}

bool ShotQueue::oldest(QueuedShot &out) {
  rescan();
  while (gCount > 0) {
    const int index = oldestIndex();
    const uint32_t seq = gItems[index].sequence;
    const bool onFlash = gItems[index].onFlash;
    if (readMeta(medium(onFlash), seq, onFlash, out)) {
      if (out.jpegLength == 0) {
        out.jpegLength = gItems[index].jpegLength;
      }
      return true;
    }
    removeFiles(medium(onFlash), seq);
    rescan();
    notify(Event::Dropped, seq);
  }
  return false;
}

bool ShotQueue::load(uint32_t sequence, uint8_t **jpeg, size_t *length) {
  if (jpeg == nullptr || length == nullptr || sequence == 0) {
    return false;
  }
  *jpeg = nullptr;
  *length = 0;
  rescan();
  const int index = indexOf(sequence);
  if (index < 0) {
    return false;
  }
  char path[40];
  pathFor(sequence, "jpg", path, sizeof(path));
  File file = medium(gItems[index].onFlash).open(path, FILE_READ);
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
  if (got != len || buf[0] != 0xff || buf[1] != 0xd8) {
    free(buf);
    return false;
  }
  *jpeg = buf;
  *length = len;
  return true;
}

void ShotQueue::release(uint8_t *jpeg) { free(jpeg); }

bool ShotQueue::ack(uint32_t sequence) {
  if (sequence == 0) {
    return false;
  }
  bool removed = false;
  if (Storage::ready()) {
    removed = removeFiles(SD_MMC, sequence) || removed;
  }
  if (gFlash) {
    removed = removeFiles(LittleFS, sequence) || removed;
  }
  rescan();
  return removed;
}

void ShotQueue::logStatus() {
  Console::printf("QUEUE depth=%d last=%lu sd=%d flash=%d\n", gCount,
                  static_cast<unsigned long>(gLastSeq), Storage::ready() ? 1 : 0,
                  gFlash ? 1 : 0);
}

const char *ShotQueue::eventName(Event event) {
  switch (event) {
    case Event::Enqueued:
      return "enqueued";
    case Event::Evicted:
      return "evicted";
    case Event::Dropped:
      return "dropped";
    case Event::SyncStarted:
      return "sync-start";
    case Event::SyncFailed:
      return "sync-fail";
    case Event::SyncAcked:
      return "sync-ack";
  }
  return "queue";
}

bool ShotQueue::formatDeviceTime(char *out, size_t outLen) {
  if (out == nullptr || outLen < 20) {
    return false;
  }
  out[0] = '\0';
  const time_t now = time(nullptr);
  if (now < static_cast<time_t>(1700000000)) {
    return false;
  }
  struct tm local {};
  localtime_r(&now, &local);
  char base[24];
  char zone[8];
  if (strftime(base, sizeof(base), "%Y-%m-%dT%H:%M:%S", &local) == 0) {
    return false;
  }
  if (strftime(zone, sizeof(zone), "%z", &local) == 0 || strlen(zone) != 5) {
    snprintf(out, outLen, "%s", base);
    return true;
  }
  snprintf(out, outLen, "%s%c%c%c:%c%c", base, zone[0], zone[1], zone[2], zone[3],
           zone[4]);
  return true;
}

}  // namespace Somnium
