#include "OfflineSync.h"

#include <stdlib.h>
#include <string.h>

#include "Config.h"
#include "Console.h"
#include "Host.h"
#include "Power.h"
#include "ShotQueue.h"
#include "Storage.h"

namespace Somnium {
namespace {

OfflineSync::PumpFn gPump = nullptr;
unsigned long gLinkUntil = 0;
unsigned long gNextAttempt = 0;
int gFailures = 0;

bool due(unsigned long deadline) {
  return static_cast<int32_t>(millis() - deadline) >= 0;
}

uint32_t backoffMs(int failures) {
  uint32_t delay = Defaults::kQueueRetryBaseMs;
  for (int i = 1; i < failures && delay < Defaults::kQueueRetryMaxMs; ++i) {
    if (delay > Defaults::kQueueRetryMaxMs / 2) {
      return Defaults::kQueueRetryMaxMs;
    }
    delay *= 2;
  }
  if (delay > Defaults::kQueueRetryMaxMs) {
    return Defaults::kQueueRetryMaxMs;
  }
  return delay;
}

void saveGallery(const QueuedShot &shot, const uint8_t *jpeg, size_t length,
                 JpegBuffer &dream) {
  if (!Storage::ready() || shot.style[0] == '\0') {
    return;
  }
  const int number = Storage::nextShotNumber(shot.style, 1);
  char path[72];
  snprintf(path, sizeof(path), "/somnium/%s_%d_0_orig.jpg", shot.style, number);
  if (!Storage::saveJpeg(path, jpeg, length)) {
    Console::printf("QUEUE gallery orig fail seq=%lu\n",
                    static_cast<unsigned long>(shot.sequence));
  }
  if (dream.data != nullptr && dream.length > 0) {
    snprintf(path, sizeof(path), "/somnium/%s_%d_1_dream.jpg", shot.style, number);
    if (!Storage::saveJpeg(path, dream.data, dream.length)) {
      Console::printf("QUEUE gallery dream fail seq=%lu\n",
                      static_cast<unsigned long>(shot.sequence));
    }
  }
}

}  // namespace

void OfflineSync::begin() {
  gLinkUntil = 0;
  gNextAttempt = 0;
  gFailures = 0;
}

void OfflineSync::setPump(PumpFn fn) { gPump = fn; }

void OfflineSync::noteLink() {
  const bool wasDown = due(gLinkUntil);
  gLinkUntil = millis() + Defaults::kLinkHoldMs;
  if (wasDown) {
    gFailures = 0;
    gNextAttempt = millis();
  }
}

bool OfflineSync::linkUp() { return !due(gLinkUntil); }

void OfflineSync::tick() {
  if (Power::busy() || !linkUp() || !due(gNextAttempt)) {
    return;
  }
  if (ShotQueue::depth() <= 0) {
    return;
  }

  QueuedShot shot;
  clearQueuedShot(shot);
  if (!ShotQueue::oldest(shot)) {
    return;
  }

  uint8_t *jpeg = nullptr;
  size_t length = 0;
  if (!ShotQueue::load(shot.sequence, &jpeg, &length)) {
    ShotQueue::ack(shot.sequence);
    ShotQueue::notify(ShotQueue::Event::Dropped, shot.sequence);
    return;
  }

  CookExtras extras;
  extras.sequence = shot.sequence;
  extras.deviceId = Power::deviceId();
  extras.batteryPctTimes10 = shot.batteryPctTimes10;
  extras.batteryMv = shot.batteryMv;
  extras.charging = shot.charging;
  extras.queueDepth = ShotQueue::depth();
  if (shot.batteryPctTimes10 >= 0) {
    extras.lowBattery =
        shot.batteryPctTimes10 <= static_cast<int>(Defaults::kLowBatteryPct * 10.0f)
            ? 1
            : 0;
  }

  ShotQueue::notify(ShotQueue::Event::SyncStarted, shot.sequence);
  Power::noteActivity();
  Power::setBusy(true);
  Host::setPump(gPump);

  JpegBuffer dream{};
  String err;
  const char *takenAt = shot.takenAt[0] != '\0' ? shot.takenAt : nullptr;
  const bool ok = Host::cook(jpeg, length, shot.style, takenAt, Defaults::kVenueLat,
                             Defaults::kVenueLon, dream, &err, &extras);
  Host::setPump(nullptr);
  Power::setBusy(false);
  Power::noteActivity();

  if (!ok) {
    ShotQueue::release(jpeg);
    gFailures += 1;
    gNextAttempt = millis() + backoffMs(gFailures);
    Console::printf("QUEUE sync fail seq=%lu in %lums (%s)\n",
                    static_cast<unsigned long>(shot.sequence),
                    static_cast<unsigned long>(backoffMs(gFailures)), err.c_str());
    ShotQueue::notify(ShotQueue::Event::SyncFailed, shot.sequence);
    if (err.indexOf("timeout") >= 0) {
      gLinkUntil = millis();
    }
    return;
  }

  saveGallery(shot, jpeg, length, dream);
  free(dream.data);
  ShotQueue::release(jpeg);
  ShotQueue::ack(shot.sequence);
  gFailures = 0;
  gNextAttempt = millis();
  Console::printf("QUEUE sync ack seq=%lu\n",
                  static_cast<unsigned long>(shot.sequence));
  ShotQueue::notify(ShotQueue::Event::SyncAcked, shot.sequence);
}

}  // namespace Somnium
