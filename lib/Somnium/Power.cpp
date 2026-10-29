#include "Power.h"

#include <Arduino.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>

#include "Camera.h"
#include "Config.h"
#include "Console.h"
#include "Display.h"
#include "Link.h"
#include "Storage.h"

#if __has_include(<esp_bt.h>)
#include <esp_bt.h>
#define SOMNIUM_HAS_ESP_BT 1
#endif

namespace Somnium {
namespace {

// ESP32-S3 deep sleep can wake only from RTC GPIOs (GPIO0–21).
// This tree's shutter is GPIO3, which is in that range, so the shutter
// is the wake button. The planned Kickstarter map (shutter on D6/GPIO43)
// is not what Config.h uses; GPIO43 could not wake the chip.
//
// The encoder push is GPIO0. That pin is also RTC-capable, but it is the
// BOOT strap: a low level while the wake reset is sampled enters the ROM
// download boot and the sketch never starts. Encoder CLK/DT are GPIO42
// and GPIO41, which are not RTC GPIOs, so a turn cannot wake the chip.
static_assert(Pins::kShutterButton >= 0 && Pins::kShutterButton <= 21,
              "shutter must stay on an RTC GPIO (0-21) to wake from deep sleep");
static_assert(Pins::kShutterButton != 0,
              "GPIO0 is the BOOT strap and cannot be the deep-sleep wake pin");

unsigned long sLastActivityMs = 0;
int sBusyDepth = 0;
esp_sleep_wakeup_cause_t sWakeCause = ESP_SLEEP_WAKEUP_UNDEFINED;

void releaseSleepHolds() {
  gpio_deep_sleep_hold_dis();
  const gpio_num_t pins[] = {
      static_cast<gpio_num_t>(Pins::kDisplaySck),
      static_cast<gpio_num_t>(Pins::kDisplayMosi),
      static_cast<gpio_num_t>(Pins::kDisplayDc),
      static_cast<gpio_num_t>(Pins::kDisplayCs),
      static_cast<gpio_num_t>(Pins::kDisplayRst),
  };
  for (gpio_num_t pin : pins) {
    gpio_hold_dis(pin);
  }
  // A previous sleep hands the shutter and the BOOT pin to the RTC mux.
  // Digital reads miss the shutter until that mux is released.
  rtc_gpio_deinit(GPIO_NUM_0);
  rtc_gpio_deinit(static_cast<gpio_num_t>(Pins::kShutterButton));
}

void holdDisplayPins() {
  const gpio_num_t pins[] = {
      static_cast<gpio_num_t>(Pins::kDisplaySck),
      static_cast<gpio_num_t>(Pins::kDisplayMosi),
      static_cast<gpio_num_t>(Pins::kDisplayDc),
      static_cast<gpio_num_t>(Pins::kDisplayCs),
      static_cast<gpio_num_t>(Pins::kDisplayRst),
  };
  for (gpio_num_t pin : pins) {
    gpio_hold_en(pin);
  }
  gpio_deep_sleep_hold_en();
}

void stopBluetooth() {
#if defined(CONFIG_BT_ENABLED) && defined(SOMNIUM_HAS_ESP_BT)
  const esp_bt_controller_status_t status = esp_bt_controller_get_status();
  if (status == ESP_BT_CONTROLLER_STATUS_IDLE) {
    Console::println("BT idle");
    return;
  }
  if (status == ESP_BT_CONTROLLER_STATUS_ENABLED) {
    esp_bt_controller_disable();
  }
  if (esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_IDLE) {
    esp_bt_controller_deinit();
  }
  Console::println("BT off");
#else
  Console::println("BT idle");
#endif
}

bool armWake() {
  const gpio_num_t shutter = static_cast<gpio_num_t>(Pins::kShutterButton);
  if (!esp_sleep_is_valid_wakeup_gpio(shutter)) {
    Console::println("ERR sleep wake pin");
    return false;
  }

  // ext0 only fires while the RTC IO domain stays up, and that domain is
  // what holds the internal pull-ups. The shutter and the BOOT pin have
  // no external resistor in this firmware; pinMode(INPUT_PULLUP) does not
  // survive deep sleep on its own.
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  const esp_err_t pd =
      esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
  if (pd != ESP_OK) {
    Console::printf("ERR sleep pd 0x%x\n", static_cast<unsigned>(pd));
    return false;
  }
  const esp_err_t err = esp_sleep_enable_ext0_wakeup(shutter, 0);
  if (err != ESP_OK) {
    rtc_gpio_deinit(shutter);
    Console::printf("ERR sleep wake 0x%x\n", static_cast<unsigned>(err));
    return false;
  }
  // Active-low shutter. The RTC pull-up has to stay on through deep sleep;
  // the digital pull configured by pinMode does not.
  rtc_gpio_pulldown_dis(shutter);
  rtc_gpio_pullup_en(shutter);

  // Keep the encoder switch / BOOT pin pulled up so the wake reset does
  // not fall into download mode when the dial click is released.
  rtc_gpio_init(GPIO_NUM_0);
  rtc_gpio_set_direction(GPIO_NUM_0, RTC_GPIO_MODE_INPUT_ONLY);
  rtc_gpio_pulldown_dis(GPIO_NUM_0);
  rtc_gpio_pullup_en(GPIO_NUM_0);
  return true;
}

bool buttonHeld() {
  return digitalRead(Pins::kShutterButton) == LOW ||
         digitalRead(Pins::kEncoderSw) == LOW;
}

}  // namespace

Power::Busy::Busy() { sBusyDepth += 1; }

Power::Busy::~Busy() {
  if (sBusyDepth > 0) {
    sBusyDepth -= 1;
  }
}

void Power::begin() {
  releaseSleepHolds();
  sWakeCause = esp_sleep_get_wakeup_cause();
  sLastActivityMs = millis();
  if (sWakeCause == ESP_SLEEP_WAKEUP_EXT0) {
    Console::println("wake shutter");
  }
  Console::printf("SLEEP idle %lus wake gpio%d\n",
                  static_cast<unsigned long>(Defaults::kInactivitySleepMs / 1000UL),
                  Pins::kShutterButton);
}

void Power::noteActivity() { sLastActivityMs = millis(); }

bool Power::wokeFromSleep() { return sWakeCause == ESP_SLEEP_WAKEUP_EXT0; }

void Power::enterDeepSleep() {
  if (!armWake()) {
    // Leave the camera, radio, and panel up. Try again after a full timeout.
    noteActivity();
    return;
  }

  Console::println("SLEEP inactivity");
  Serial.flush();

  // Panel first so the backlight-side software-off happens before the
  // longer radio teardown. Holds latch CS/RST so deep sleep cannot float
  // the bus back into a white frame.
  if (Display::shutdown()) {
    holdDisplayPins();
  }
  Camera::end();
  Storage::end();
  linkShutdown();
  stopBluetooth();
  Serial.flush();
  delay(20);
  esp_deep_sleep_start();
}

void Power::poll() {
  if (sBusyDepth > 0) {
    return;
  }
  if (millis() - sLastActivityMs < Defaults::kInactivitySleepMs) {
    return;
  }
  // ext0 is level-triggered. Sleeping while the shutter is already low
  // would wake immediately. GPIO0 held low at that wake would enter the
  // ROM download boot, so wait until the dial click is released too.
  if (buttonHeld()) {
    return;
  }
  enterDeepSleep();
}

}  // namespace Somnium
