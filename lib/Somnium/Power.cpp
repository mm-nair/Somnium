#include "Power.h"

#include <esp_sleep.h>
#include <driver/gpio.h>
#include <driver/uart.h>
#include <soc/soc_caps.h>
#include <stdio.h>

#include "BatteryCurve.h"
#include "Config.h"
#include "Console.h"

namespace Somnium {
namespace {

BatterySample gSample;
Power::Listener gListener = nullptr;
Power::DepthFn gDepth = nullptr;
Power::SeqFn gLastSeq = nullptr;
char gDeviceId[24] = "somnium";

bool gLightEnabled = Defaults::kLightSleepEnabled;
bool gDeepEnabled = Defaults::kDeepSleepEnabled;
bool gIdle = false;
bool gBusy = false;
bool gSeenLink = false;
bool gAnnouncedLight = false;
bool gWokeFromShutter = false;
bool gPendingWake = false;
unsigned long gLastActivity = 0;
unsigned long gLastLink = 0;
unsigned long gLastSample = 0;
unsigned long gLastStatus = 0;

int pctTimes10FromSample() {
  if (!gSample.valid) {
    return -1;
  }
  int pct = static_cast<int>(gSample.percent * 10.0f + 0.5f);
  if (pct < 0) {
    pct = 0;
  }
  if (pct > 1000) {
    pct = 1000;
  }
  return pct;
}

void emit(PowerEvent event) {
  if (gListener != nullptr) {
    gListener(event);
  }
}

bool buttonHeld() {
  return digitalRead(Pins::kShutterButton) == LOW ||
         digitalRead(Pins::kEncoderSw) == LOW;
}

bool tethered(unsigned long now) {
  if (!Defaults::kSleepOnlyWhenUntethered || !gSeenLink) {
    return false;
  }
  return static_cast<int32_t>(now - gLastLink) <
         static_cast<int32_t>(Defaults::kLinkHoldMs);
}

uint64_t wakeMask() {
  return (1ULL << static_cast<unsigned>(Pins::kShutterButton)) |
         (1ULL << static_cast<unsigned>(Pins::kEncoderSw));
}

void enterDeep() {
  emit(PowerEvent::DeepSleep);
  Console::println("POWER deep-sleep");
  Serial.flush();
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  const uint64_t mask = wakeMask();
#if defined(SOC_GPIO_SUPPORT_DEEPSLEEP_WAKEUP) && SOC_GPIO_SUPPORT_DEEPSLEEP_WAKEUP
  const esp_err_t err =
      esp_deep_sleep_enable_gpio_wakeup(mask, ESP_GPIO_WAKEUP_GPIO_LOW);
  if (err != ESP_OK) {
    Console::printf("POWER deep wake cfg %d\n", static_cast<int>(err));
    esp_sleep_enable_ext0_wakeup(static_cast<gpio_num_t>(Pins::kShutterButton),
                                 0);
  }
#else
  esp_sleep_enable_ext0_wakeup(static_cast<gpio_num_t>(Pins::kShutterButton), 0);
  (void)mask;
#endif
  esp_deep_sleep_start();
}

void enterLight(uint32_t idleMs) {
  if (!gAnnouncedLight) {
    gAnnouncedLight = true;
    emit(PowerEvent::LightSleep);
    Console::println("POWER light-sleep");
    Serial.flush();
  }
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  gpio_wakeup_enable(static_cast<gpio_num_t>(Pins::kShutterButton),
                     GPIO_INTR_LOW_LEVEL);
  gpio_wakeup_enable(static_cast<gpio_num_t>(Pins::kEncoderSw),
                     GPIO_INTR_LOW_LEVEL);
  esp_sleep_enable_gpio_wakeup();
  uart_set_wakeup_threshold(UART_NUM_0, 3);
  esp_sleep_enable_uart_wakeup(UART_NUM_0);
  // Slice light sleep so a long idle can still fall through to deep sleep.
  // A button or UART byte wakes the slice early.
  if (gDeepEnabled && idleMs < Defaults::kDeepSleepAfterMs) {
    uint32_t slice = Defaults::kDeepSleepAfterMs - idleMs;
    if (slice < 50) {
      slice = 50;
    }
    if (slice > Defaults::kLightSleepSliceMs) {
      slice = Defaults::kLightSleepSliceMs;
    }
    esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(slice) * 1000ULL);
  }
  esp_light_sleep_start();
  gpio_wakeup_disable(static_cast<gpio_num_t>(Pins::kShutterButton));
  gpio_wakeup_disable(static_cast<gpio_num_t>(Pins::kEncoderSw));
  if (buttonHeld()) {
    gLastActivity = millis();
    gAnnouncedLight = false;
  }
}

void maybeSleep(unsigned long now) {
  if (gBusy || !gIdle) {
    return;
  }
  if (now < Defaults::kBootGraceMs) {
    return;
  }
  if (tethered(now)) {
    return;
  }
  if (buttonHeld()) {
    gLastActivity = now;
    return;
  }
  const uint32_t idle = now - gLastActivity;
  if (gDeepEnabled && idle >= Defaults::kDeepSleepAfterMs) {
    enterDeep();
  }
  if (gLightEnabled && idle >= Defaults::kLightSleepAfterMs) {
    enterLight(idle);
  }
}

}  // namespace

void Power::begin() {
  gLightEnabled = Defaults::kLightSleepEnabled;
  gDeepEnabled = Defaults::kDeepSleepEnabled;
  gLastActivity = millis();
  gPendingWake = false;
  gWokeFromShutter = false;

  if (Defaults::kDeviceId[0] != '\0') {
    strncpy(gDeviceId, Defaults::kDeviceId, sizeof(gDeviceId) - 1);
    gDeviceId[sizeof(gDeviceId) - 1] = '\0';
  } else {
    const uint64_t mac = ESP.getEfuseMac();
    snprintf(gDeviceId, sizeof(gDeviceId), "esp%08lx%08lx",
             static_cast<unsigned long>(mac >> 32),
             static_cast<unsigned long>(mac & 0xffffffffUL));
  }

  const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  uint64_t bits = 0;
  if (cause == ESP_SLEEP_WAKEUP_EXT0) {
    gWokeFromShutter = true;
  } else if (cause == ESP_SLEEP_WAKEUP_EXT1) {
    bits = esp_sleep_get_ext1_wakeup_status();
  }
#if defined(SOC_GPIO_SUPPORT_DEEPSLEEP_WAKEUP) && SOC_GPIO_SUPPORT_DEEPSLEEP_WAKEUP
  if (cause == ESP_SLEEP_WAKEUP_GPIO) {
    bits = esp_sleep_get_gpio_wakeup_status();
  }
#endif
  if ((bits & (1ULL << static_cast<unsigned>(Pins::kShutterButton))) != 0) {
    gWokeFromShutter = true;
  }
  if (cause != ESP_SLEEP_WAKEUP_UNDEFINED) {
    gPendingWake = true;
  }
  Console::printf("POWER wake cause=%d bits=%lu:%lu shutter=%d id=%s\n",
                  static_cast<int>(cause),
                  static_cast<unsigned long>(bits >> 32),
                  static_cast<unsigned long>(bits & 0xffffffffUL),
                  gWokeFromShutter ? 1 : 0, gDeviceId);

  if (Pins::kChargeSense >= 0) {
    pinMode(Pins::kChargeSense, INPUT_PULLUP);
  }
  if (Defaults::kBatteryMonitor && Pins::kBatteryAdc >= 0) {
    pinMode(Pins::kBatteryAdc, INPUT);
    analogReadResolution(12);
#if defined(ADC_11db)
    analogSetPinAttenuation(Pins::kBatteryAdc, ADC_11db);
#endif
  }
}

void Power::tick() {
  const unsigned long now = millis();
  if (gPendingWake) {
    gPendingWake = false;
    emit(PowerEvent::Wake);
  }
  if (Defaults::kBatteryMonitor &&
      (gLastSample == 0 || now - gLastSample >= Defaults::kBatterySampleMs)) {
    gLastSample = now;
    const bool wasLow = gSample.low;
    const bool prevCharge = gSample.charging;
    const bool prevKnown = gSample.chargeKnown;

    BatterySample next;
    if (Pins::kBatteryAdc >= 0) {
      uint32_t sum = 0;
      for (int i = 0; i < 8; ++i) {
        sum += static_cast<uint32_t>(analogRead(Pins::kBatteryAdc));
      }
      next.raw = static_cast<int>(sum / 8);
      next.volts = voltageFromDivider(next.raw, Defaults::kAdcFullScaleVolts,
                                      Defaults::kDividerRatio, Defaults::kAdcMax);
      next.percent = percentFromVoltage(next.volts);
      // A floating pin often rails. Mid-scale noise still looks like a cell;
      // treat only the rails as "not wired".
      next.valid = next.raw > 80 && next.raw < (Defaults::kAdcMax - 80);
    }
    if (Pins::kChargeSense >= 0) {
      next.chargeKnown = true;
      next.charging = digitalRead(Pins::kChargeSense) == LOW;
    }
    if (next.valid && next.percent <= Defaults::kLowBatteryPct) {
      next.low = true;
    } else if (next.valid && next.percent >= Defaults::kLowBatteryPct + 2.0f) {
      next.low = false;
    } else {
      next.low = wasLow;
    }
    gSample = next;
    emit(PowerEvent::Sample);
    if (gSample.low && !wasLow) {
      emit(PowerEvent::LowBattery);
    }
    if (gSample.chargeKnown && prevKnown && gSample.charging != prevCharge) {
      emit(PowerEvent::ChargeChanged);
    }
  }
  if (!gBusy && (gLastStatus == 0 ||
                 now - gLastStatus >= Defaults::kStatusPeriodMs)) {
    gLastStatus = now;
    writeStatus();
  }
  maybeSleep(now);
}

void Power::noteActivity() {
  gLastActivity = millis();
  gAnnouncedLight = false;
}

void Power::noteLinkActivity() {
  gLastLink = millis();
  gSeenLink = true;
}

void Power::setIdle(bool idle) { gIdle = idle; }

void Power::setBusy(bool busy) { gBusy = busy; }

bool Power::busy() { return gBusy; }

void Power::setSleepEnabled(bool lightSleep, bool deepSleep) {
  gLightEnabled = lightSleep;
  gDeepEnabled = deepSleep;
  Console::printf("POWER sleep light=%d deep=%d\n", lightSleep ? 1 : 0,
                  deepSleep ? 1 : 0);
}

void Power::setListener(Listener listener) { gListener = listener; }

void Power::setQueueDepth(DepthFn fn) { gDepth = fn; }

void Power::setLastSequence(SeqFn fn) { gLastSeq = fn; }

const BatterySample &Power::last() { return gSample; }

const char *Power::deviceId() { return gDeviceId; }

bool Power::consumeWakeShutter() {
  const bool woke = gWokeFromShutter;
  gWokeFromShutter = false;
  return woke;
}

int Power::percentTimes10() { return pctTimes10FromSample(); }

int Power::millivolts() {
  if (!gSample.valid) {
    return -1;
  }
  int mv = static_cast<int>(gSample.volts * 1000.0f + 0.5f);
  if (mv < 0) {
    mv = 0;
  }
  return mv;
}

int Power::chargingFlag() {
  if (!gSample.chargeKnown) {
    return -1;
  }
  return gSample.charging ? 1 : 0;
}

int Power::lowFlag() {
  if (!gSample.valid) {
    return -1;
  }
  return gSample.low ? 1 : 0;
}

void Power::writeStatus() {
  Serial.printf("STATUS dev=%s", gDeviceId);
  const int pct = percentTimes10();
  const int mv = millivolts();
  if (pct >= 0 && mv >= 0) {
    int frac = pct % 10;
    if (frac < 0) {
      frac = -frac;
    }
    Serial.printf(" bat=%d.%d mv=%d low=%d", pct / 10, frac, mv,
                  gSample.low ? 1 : 0);
  }
  const int chg = chargingFlag();
  if (chg >= 0) {
    Serial.printf(" chg=%d", chg);
  }
  if (gDepth != nullptr) {
    const int depth = gDepth();
    if (depth >= 0) {
      Serial.printf(" q=%d", depth);
    }
  }
  if (gLastSeq != nullptr) {
    Serial.printf(" seq=%lu", static_cast<unsigned long>(gLastSeq()));
  }
  Serial.print("\n");
  Serial.flush();
}

const char *Power::eventName(PowerEvent event) {
  switch (event) {
    case PowerEvent::Sample:
      return "sample";
    case PowerEvent::LowBattery:
      return "low";
    case PowerEvent::ChargeChanged:
      return "charge";
    case PowerEvent::LightSleep:
      return "light-sleep";
    case PowerEvent::DeepSleep:
      return "deep-sleep";
    case PowerEvent::Wake:
      return "wake";
  }
  return "power";
}

}  // namespace Somnium
