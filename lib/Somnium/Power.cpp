#include "Power.h"

#include <driver/gpio.h>
#include <esp32-hal-adc.h>
#include <esp_sleep.h>

#include "Ble.h"
#include "Camera.h"
#include "Config.h"
#include "Console.h"
#include "Display.h"
#include "Inputs.h"
#include "Transfer.h"

namespace Somnium {
namespace {

int sBusy = 0;
unsigned long sRetryAt = 0;
unsigned long sLastBatteryMs = 0;
int sBatteryMv = 0;
bool sBatteryKnown = false;

constexpr int kWakePins[] = {
    Pins::kShutterButton,
    Pins::kEncoderClk,
    Pins::kEncoderDt,
    Pins::kSelectButton,
};

const char *wakeName(esp_sleep_wakeup_cause_t cause) {
  switch (cause) {
    case ESP_SLEEP_WAKEUP_GPIO:
      return "gpio";
    case ESP_SLEEP_WAKEUP_TIMER:
      return "timer";
    case ESP_SLEEP_WAKEUP_UART:
      return "uart";
    default:
      return "other";
  }
}

void disarmWake() {
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  for (int pin : kWakePins) {
    gpio_wakeup_disable(static_cast<gpio_num_t>(pin));
  }
}

bool armWake() {
  for (int pin : kWakePins) {
    // Level wake fires immediately if the line is already asserted.
    // Encoder A/B idle high at a detent; shutter and select idle high.
    if (digitalRead(pin) != HIGH) {
      return false;
    }
  }
  disarmWake();
  for (int pin : kWakePins) {
    const esp_err_t err = gpio_wakeup_enable(static_cast<gpio_num_t>(pin),
                                             GPIO_INTR_LOW_LEVEL);
    if (err != ESP_OK) {
      disarmWake();
      return false;
    }
  }
  if (esp_sleep_enable_gpio_wakeup() != ESP_OK) {
    disarmWake();
    return false;
  }
  return true;
}

void restoreRadios(bool cameraWasReady) {
  Display::setActive(true);
  if (cameraWasReady) {
    if (!Camera::begin(Defaults::kCameraWidth)) {
      Console::println("ERR camera after wake");
    }
  }
  Ble::resume();
}

bool enterLightSleep() {
  if (!armWake()) {
    return false;
  }
  const bool cameraWasReady = Camera::ready();
  Console::println("SLEEP light");
  Serial.flush();

  Transfer::shutdown();
  Ble::suspend();
  if (cameraWasReady) {
    Camera::end();
  }
  Display::setActive(false);
  Serial.flush();

  const esp_err_t slept = esp_light_sleep_start();
  const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  disarmWake();
  Inputs::settleEncoder();
  Inputs::noteActivity();

  if (slept != ESP_OK) {
    Console::printf("SLEEP rejected 0x%x\n", static_cast<unsigned>(slept));
    restoreRadios(cameraWasReady);
    sRetryAt = millis() + 3000;
    return false;
  }
  Console::printf("SLEEP wake %s\n", wakeName(cause));
  restoreRadios(cameraWasReady);
  return true;
}

}  // namespace

void Power::begin() {
  sBusy = 0;
  sRetryAt = 0;
  sBatteryKnown = false;
  analogSetPinAttenuation(Pins::kBatteryAdc, ADC_11db);
  Inputs::noteActivity();
  Console::printf("PWR sleep %lu ms; battery ADC GPIO%d\n",
                  static_cast<unsigned long>(Defaults::kInactivitySleepMs),
                  Pins::kBatteryAdc);
}

void Power::setBusy(bool busy) {
  if (busy) {
    ++sBusy;
    return;
  }
  if (sBusy > 0) {
    --sBusy;
  }
  if (sBusy == 0) {
    // A capture, save, or transfer is not idle time.
    Inputs::noteActivity();
  }
}

bool Power::busy() { return sBusy > 0; }

void Power::sampleBattery() {
  if (sBusy > 0 || Transfer::radioUp() || Ble::previewBusy()) {
    return;
  }
  const unsigned long now = millis();
  if (sBatteryKnown && now - sLastBatteryMs < Defaults::kBatterySampleMs) {
    return;
  }
  sLastBatteryMs = now;
  uint32_t sum = 0;
  for (int i = 0; i < Defaults::kBatterySamples; ++i) {
    sum += analogReadMilliVolts(Pins::kBatteryAdc);
  }
  const int pinMv = static_cast<int>(sum / Defaults::kBatterySamples);
  const int packMv = pinMv * (Defaults::kBatteryTopOhms + Defaults::kBatteryBottomOhms) /
                     Defaults::kBatteryBottomOhms;
  if (!sBatteryKnown) {
    sBatteryMv = packMv;
    sBatteryKnown = true;
  } else {
    sBatteryMv = (sBatteryMv * 3 + packMv) / 4;
  }
}

bool Power::batteryMillivolts(int &millivolts) {
  if (!sBatteryKnown) {
    millivolts = -1;
    return false;
  }
  millivolts = sBatteryMv;
  return true;
}

int Power::batteryPercent() {
  if (!sBatteryKnown) {
    return -1;
  }
  const int span = Defaults::kBatteryFullMv - Defaults::kBatteryEmptyMv;
  int pct = (sBatteryMv - Defaults::kBatteryEmptyMv) * 100 / span;
  if (pct < 0) {
    pct = 0;
  }
  if (pct > 100) {
    pct = 100;
  }
  return pct;
}

bool Power::batteryLow() {
  const int pct = batteryPercent();
  return pct >= 0 && pct <= Defaults::kBatteryLowPercent;
}

bool Power::poll() {
  if (sBusy > 0 || Transfer::radioUp() || Ble::previewBusy()) {
    return false;
  }
  if (Inputs::shutterDown() || Inputs::encoderButtonDown()) {
    return false;
  }
  if (Serial.available() > 0) {
    Inputs::noteActivity();
    return false;
  }
  const unsigned long now = millis();
  if (sRetryAt != 0 && now < sRetryAt) {
    return false;
  }
  if (now - Inputs::lastActivityMs() < Defaults::kInactivitySleepMs) {
    return false;
  }
  return enterLightSleep();
}

}  // namespace Somnium
