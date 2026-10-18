#pragma once

#include <Arduino.h>

namespace Somnium {

struct BatterySample {
  float volts = 0;
  float percent = 0;
  int raw = 0;
  bool valid = false;
  bool charging = false;
  bool chargeKnown = false;
  bool low = false;
};

enum class PowerEvent : uint8_t {
  Sample = 0,
  LowBattery,
  ChargeChanged,
  LightSleep,
  DeepSleep,
  Wake,
};

class Power {
 public:
  using Listener = void (*)(PowerEvent event);
  using DepthFn = int (*)();
  using SeqFn = uint32_t (*)();

  static void begin();
  static void tick();
  static void noteActivity();
  static void noteLinkActivity();
  static void setIdle(bool idle);
  static void setBusy(bool busy);
  static bool busy();
  static void setSleepEnabled(bool lightSleep, bool deepSleep);
  static void setListener(Listener listener);
  static void setQueueDepth(DepthFn fn);
  static void setLastSequence(SeqFn fn);

  static const BatterySample &last();
  static const char *deviceId();
  static bool consumeWakeShutter();
  static int percentTimes10();
  static int millivolts();
  static int chargingFlag();
  static int lowFlag();
  static void writeStatus();
  static const char *eventName(PowerEvent event);
};

}  // namespace Somnium
