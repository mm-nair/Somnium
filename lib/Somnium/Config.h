#pragma once

#include <Arduino.h>

namespace Somnium {
namespace Pins {

// Shared SPI bus. The Sense expansion already wires SCK/MISO/MOSI/CS to the
// microSD socket. The 1.14" ST7789 uses SCK and MOSI and its own CS. MISO
// stays free for the card. The panel must have a CS pin; a CS-less 240x240
// module cannot share this bus.
constexpr int kSpiSck = 7;     // D8
constexpr int kSpiMiso = 8;    // D9, SD data out
constexpr int kSpiMosi = 9;    // D10, panel SDA
constexpr int kSdCs = 21;      // Sense microSD CS, not on the header
constexpr int kDisplayCs = 1;  // D0
constexpr int kDisplayDc = 2;  // D1
constexpr int kDisplayRst = 3; // D2, RES. Strap pin: keep high at reset.
constexpr int kDisplayBl = 44; // D7, BLK, active high

// Encoder A/B always. Select is the encoder push switch OR a separate
// tactile button on this same net (input pull-up, active low). Leave the
// pad unconnected if neither is fitted; the pull-up holds it idle.
constexpr int kEncoderClk = 4;  // D3, encoder A
constexpr int kEncoderDt = 5;   // D4, encoder B
constexpr int kSelectButton = 6; // D5
constexpr int kEncoderSw = kSelectButton;
constexpr int kShutterButton = 43; // D6

// Seeed XIAO ESP32S3 Sense camera (OV5640 on the expansion FPC).
constexpr int kCamXclk = 10;
constexpr int kCamSda = 40;
constexpr int kCamScl = 39;
constexpr int kCamD0 = 15;
constexpr int kCamD1 = 17;
constexpr int kCamD2 = 18;
constexpr int kCamD3 = 16;
constexpr int kCamD4 = 14;
constexpr int kCamD5 = 12;
constexpr int kCamD6 = 11;
constexpr int kCamD7 = 48;
constexpr int kCamVsync = 38;
constexpr int kCamHref = 47;
constexpr int kCamPclk = 13;
constexpr int kCamPwdn = -1;
constexpr int kCamReset = -1;

// No header pin is left that is both free and ADC1-capable. GPIO44 is the
// backlight and is not an ADC pin. Battery voltage is not measured.

}  // namespace Pins

namespace Defaults {

constexpr uint32_t kSerialBaud = 115200;
constexpr uint32_t kDisplaySpiHz = 40000000;
// 1.14" ST7789 is 135x240 native. Rotation 1 presents 240x135 landscape.
constexpr int kDisplayWidth = 240;
constexpr int kDisplayHeight = 135;
constexpr uint8_t kDisplayRotation = 1;
constexpr bool kDisplayInvert = true;
constexpr bool kDisplayBlActiveHigh = true;

// Sensor JPEG size. SVGA (800x600) is the default; the phone transfer is
// chunked so a later bench can move this up. Mapped in Camera.cpp.
constexpr int kCameraWidth = 800;
constexpr int kCameraHeight = 600;
constexpr bool kCameraMirrorHorizontal = true;
constexpr int kJpegQuality = 12;  // esp_camera scale: lower is higher quality
constexpr int kPreviewWidth = 120;
constexpr int kPreviewHeight = 68;
constexpr int kPreviewJpegQuality = 35;

constexpr unsigned long kButtonDebounceMs = 25;
// Hold the shutter this long to fire logical select when no select
// button is fitted. A short press still takes the photo.
constexpr unsigned long kSelectHoldMs = 550;
// Idle time with no shutter, encoder, or select input before light sleep.
constexpr uint32_t kInactivitySleepMs = 45000;
// SoftAP with no associated phone and no bytes is torn down so sleep can run.
constexpr uint32_t kSoftApIdleMs = 60000;
// Must exceed the phone's relay to the local API plus reconnect slack.
constexpr uint32_t kCookTimeoutMs = 300000;

constexpr uint16_t kLinkPort = 80;
// Kept so the dormant station-mode link still compiles. This board does
// not join that network; Transfer owns the SoftAP.
constexpr char kLinkHostname[] = "somnium";
constexpr char kBleName[] = "Somnium";
constexpr char kSoftApPrefix[] = "Somnium-";

constexpr float kVenueLat = 47.6553f;
constexpr float kVenueLon = -122.3035f;

}  // namespace Defaults
}  // namespace Somnium
