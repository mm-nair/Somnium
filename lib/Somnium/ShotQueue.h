#pragma once

#include <Arduino.h>
#include <string.h>

namespace Somnium {

struct QueuedShot {
  uint32_t sequence;
  char style[24];
  char takenAt[40];
  int batteryPctTimes10;
  int batteryMv;
  int charging;
  size_t jpegLength;
  bool onFlash;
};

inline void clearQueuedShot(QueuedShot &shot) {
  memset(&shot, 0, sizeof(shot));
  shot.batteryPctTimes10 = -1;
  shot.batteryMv = -1;
  shot.charging = -1;
}

class ShotQueue {
 public:
  enum class Event : uint8_t {
    Enqueued = 0,
    Evicted,
    Dropped,
    SyncStarted,
    SyncFailed,
    SyncAcked,
  };
  using Listener = void (*)(Event event, uint32_t sequence, int depth);

  static bool begin();
  static void setListener(Listener listener);
  static void notify(Event event, uint32_t sequence);

  static uint32_t allocateSequence();
  static uint32_t lastSequence();
  static int depth();

  // Already-stored sequence ids are kept (dedupe) and reported as success.
  static bool enqueue(const QueuedShot &meta, const uint8_t *jpeg, size_t length);
  static bool oldest(QueuedShot &out);
  static bool load(uint32_t sequence, uint8_t **jpeg, size_t *length);
  static void release(uint8_t *jpeg);
  static bool ack(uint32_t sequence);
  static void logStatus();
  static const char *eventName(Event event);

  // ISO-8601 with a colon in the offset, or false if the clock is unset.
  static bool formatDeviceTime(char *out, size_t outLen);
};

}  // namespace Somnium
