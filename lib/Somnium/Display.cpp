#include "Display.h"

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Adafruit_ST77xx.h>
#include <SPI.h>

#include "Config.h"
#include "Console.h"

namespace Somnium {
namespace {

Adafruit_ST7789 *gTft = nullptr;
bool gReady = false;
bool gActive = false;

void writeBacklight(bool on) {
  if (Pins::kDisplayBl < 0) {
    return;
  }
  const bool level = on == Defaults::kDisplayBlActiveHigh;
  digitalWrite(Pins::kDisplayBl, level ? HIGH : LOW);
}

}  // namespace

bool Display::begin() {
  if (gReady) {
    return true;
  }
  SPI.begin(Pins::kSpiSck, Pins::kSpiMiso, Pins::kSpiMosi, -1);
  pinMode(Pins::kSdCs, OUTPUT);
  digitalWrite(Pins::kSdCs, HIGH);
  pinMode(Pins::kDisplayCs, OUTPUT);
  digitalWrite(Pins::kDisplayCs, HIGH);
  pinMode(Pins::kDisplayBl, OUTPUT);
  writeBacklight(true);

  gTft = new Adafruit_ST7789(Pins::kDisplayCs, Pins::kDisplayDc, Pins::kDisplayRst);
  gTft->init(135, 240);
  gTft->setSPISpeed(Defaults::kDisplaySpiHz);
  gTft->setRotation(Defaults::kDisplayRotation);
  gTft->invertDisplay(Defaults::kDisplayInvert);
  gTft->fillScreen(0xF800);
  delay(80);
  gTft->fillScreen(0x07E0);
  delay(80);
  gTft->fillScreen(0x001F);
  delay(80);
  gTft->fillScreen(0x0000);
  gReady = true;
  gActive = true;
  Console::printf("LCD ST7789 %dx%d rot=%u CS=%d DC=%d RST=%d BL=%d\n",
                  Defaults::kDisplayWidth, Defaults::kDisplayHeight,
                  static_cast<unsigned>(Defaults::kDisplayRotation),
                  Pins::kDisplayCs, Pins::kDisplayDc, Pins::kDisplayRst,
                  Pins::kDisplayBl);
  return true;
}

void Display::clear(uint16_t color) {
  if (gReady && gActive && gTft != nullptr) {
    gTft->fillScreen(color);
  }
}

int Display::width() { return Defaults::kDisplayWidth; }

int Display::height() { return Defaults::kDisplayHeight; }

bool Display::ready() { return gReady && gTft != nullptr; }

Adafruit_SPITFT *Display::tft() { return gTft; }

void Display::setActive(bool on) {
  if (!gReady || gTft == nullptr || on == gActive) {
    writeBacklight(on);
    return;
  }
  if (!on) {
    writeBacklight(false);
    gTft->sendCommand(ST77XX_DISPOFF);
    gTft->sendCommand(ST77XX_SLPIN);
    gActive = false;
    Console::println("LCD sleep");
    return;
  }
  gTft->sendCommand(ST77XX_SLPOUT);
  delay(120);
  gTft->sendCommand(ST77XX_DISPON);
  writeBacklight(true);
  gActive = true;
  Console::println("LCD wake");
}

bool Display::active() { return gActive; }

}  // namespace Somnium
