#pragma once

#include <Arduino.h>

// Pin map matches the Somnium wiring diagram (rotary encoder + shutter + SPI LCD).
// Camera DVP pins are fixed by the Freenove FNK0085 board.

namespace Somnium {
namespace Pins {

// --- SPI LCD (ILI9341 / compatible 2.4") ---
constexpr int kDisplaySck = 21;
constexpr int kDisplayMosi = 47;
constexpr int kDisplayDc = 14;
constexpr int kDisplayCs = 41;
constexpr int kDisplayRst = 48;
// MISO unused; backlight is hard-wired to 3V3 via 100 Ω.

// --- Rotary encoder (TURN + PRESS) ---
constexpr int kEncoderClk = 1;
constexpr int kEncoderDt = 2;
constexpr int kEncoderSw = 42;

// --- Shutter / click button (active LOW → GND) ---
constexpr int kShutterButton = 3;

// --- Freenove onboard camera (GC0308 on this board) ---
constexpr int kCamD0 = 11;
constexpr int kCamD1 = 9;
constexpr int kCamD2 = 8;
constexpr int kCamD3 = 10;
constexpr int kCamD4 = 12;
constexpr int kCamD5 = 18;
constexpr int kCamD6 = 17;
constexpr int kCamD7 = 16;
constexpr int kCamXclk = 15;
constexpr int kCamPclk = 13;
constexpr int kCamVsync = 6;
constexpr int kCamHref = 7;
constexpr int kCamSda = 4;
constexpr int kCamScl = 5;
constexpr int kCamPwdn = -1;
constexpr int kCamReset = -1;

}  // namespace Pins

namespace Defaults {

constexpr uint32_t kSerialBaud = 115200;
constexpr uint32_t kDisplaySpiHz = 10000000;
constexpr int kDisplayWidth = 320;
constexpr int kDisplayHeight = 240;
constexpr int kCameraWidth = 320;  // 320 or 640
constexpr int kJpegQuality = 70;   // 1..95 for frame2jpg
constexpr unsigned long kButtonDebounceMs = 25;
constexpr unsigned long kWifiConnectTimeoutMs = 20000;

}  // namespace Defaults
}  // namespace Somnium
