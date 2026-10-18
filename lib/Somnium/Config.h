#pragma once

#include <Arduino.h>

namespace Somnium {
namespace Pins {

// LCD pins match Hackathon_Prep camera_dual (verified working on this hardware).
constexpr int kDisplaySck = 1;
constexpr int kDisplayMosi = 2;
constexpr int kDisplayDc = 14;
constexpr int kDisplayCs = 21;
constexpr int kDisplayRst = 47;

// Encoder pins match Hackathon_Prep controls firmware.
constexpr int kEncoderClk = 42;
constexpr int kEncoderDt = 41;
constexpr int kEncoderSw = 0;
constexpr int kShutterButton = 3;

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

constexpr uint32_t kSerialBaud = 921600;
constexpr uint32_t kDisplaySpiHz = 10000000;
constexpr int kDisplayWidth = 320;
constexpr int kDisplayHeight = 240;
constexpr int kCameraWidth = 320;
constexpr bool kCameraMirrorHorizontal = true;
constexpr int kJpegQuality = 70;
constexpr unsigned long kButtonDebounceMs = 25;
// Must exceed usb_bridge/xAI --timeout (240s) plus USB reconnect slack.
constexpr uint32_t kCookTimeoutMs = 300000;

// Venue for Open-Meteo mood (sent with each USB cook).
constexpr float kVenueLat = 47.6553f;
constexpr float kVenueLon = -122.3035f;

}  // namespace Defaults
}  // namespace Somnium

