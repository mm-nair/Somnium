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

// 1S pack sense. GPIO19 is ADC2_CH8; GPIO20 is the charger CHRG input.
// Both are free while USB-CDC is off (the cook link is the CH340 on UART0).
// ADC2 readings are only valid while the Wi-Fi radio is off.
constexpr int kBatteryAdc = 19;
constexpr int kChargeSense = 20;

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

// Empty → efuse MAC, formatted espXXXXXXXXXXXXXXXX.
constexpr char kDeviceId[] = "";

// Battery divider. Full-scale is the ESP32-S3 11 dB ADC range (~3.1 V), not 3.3 V.
constexpr bool kBatteryMonitor = true;
constexpr float kDividerRatio = 2.0f;
constexpr float kAdcFullScaleVolts = 3.1f;
constexpr int kAdcMax = 4095;
constexpr float kLowBatteryPct = 15.0f;
constexpr uint32_t kBatterySampleMs = 5000;
constexpr uint32_t kStatusPeriodMs = 30000;

// Sleep is suppressed while the USB bridge has sent TIME recently, so a
// tethered booth session stays awake. Untethered, the CPU sleeps.
constexpr bool kLightSleepEnabled = true;
constexpr bool kDeepSleepEnabled = true;
constexpr bool kSleepOnlyWhenUntethered = true;
constexpr uint32_t kLightSleepAfterMs = 20000;
constexpr uint32_t kDeepSleepAfterMs = 180000;
constexpr uint32_t kLinkHoldMs = 45000;
constexpr uint32_t kBootGraceMs = 15000;
constexpr uint32_t kLightSleepSliceMs = 5000;

// Offline COOK queue. Count cap is global; byte caps are per medium.
constexpr int kQueueMaxShots = 24;
constexpr uint32_t kQueueMaxSdBytes = 8u * 1024u * 1024u;
constexpr uint32_t kQueueMaxFlashBytes = 1024u * 1024u;
constexpr uint32_t kQueueRetryBaseMs = 2000;
constexpr uint32_t kQueueRetryMaxMs = 60000;

}  // namespace Defaults
}  // namespace Somnium

