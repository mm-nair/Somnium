#pragma once

// Phone-link payloads for the Somnium BLE GATT service.
// This header is host-testable: it does not include Arduino or NimBLE.
// Byte order is little-endian. Strings are ASCII.

#include <stddef.h>
#include <stdint.h>

namespace Somnium {
namespace BleProto {

// 128-bit UUIDs. "SOMN" / "SOMNIM" in ASCII so a sniffer can recognize them.
constexpr char kServiceUuid[] = "534f4d4e-1000-4000-8000-534f4d4e494d";
constexpr char kModesUuid[] = "534f4d4e-1001-4000-8000-534f4d4e494d";
constexpr char kModeSelectUuid[] = "534f4d4e-1002-4000-8000-534f4d4e494d";
constexpr char kShutterUuid[] = "534f4d4e-1003-4000-8000-534f4d4e494d";
constexpr char kStatusUuid[] = "534f4d4e-1004-4000-8000-534f4d4e494d";
constexpr char kDreamUuid[] = "534f4d4e-1005-4000-8000-534f4d4e494d";
constexpr char kPreviewUuid[] = "534f4d4e-1006-4000-8000-534f4d4e494d";

constexpr uint8_t kStatusVersion = 1;
// Fits in a default ATT notification (MTU 23 → 20 byte payload).
constexpr size_t kStatusBytes = 10;
constexpr size_t kDreamBytes = 20;
constexpr size_t kNameBytes = 16;
// Modes notification shorter than the packed list: client should Read.
constexpr uint8_t kModesUpdated = 0x01;

constexpr uint8_t kScreenBoot = 0;
constexpr uint8_t kScreenMode = 1;
constexpr uint8_t kScreenProcess = 2;
constexpr uint8_t kScreenReview = 3;
constexpr uint8_t kScreenStorage = 4;

constexpr uint8_t kFlagBusy = 1u << 0;
constexpr uint8_t kFlagFailed = 1u << 1;
constexpr uint8_t kFlagSd = 1u << 2;
constexpr uint8_t kFlagCamera = 1u << 3;
constexpr uint8_t kFlagDream = 1u << 4;
constexpr uint8_t kFlagBattery = 1u << 5;

// No divider on the Freenove ESP32-S3 WROOM CAM. 0xFF means "no gauge".
constexpr uint8_t kBatteryAbsent = 0xFF;

constexpr uint8_t kDreamIdle = 0;
constexpr uint8_t kDreamReady = 1;
constexpr uint8_t kDreamFailed = 2;

constexpr uint8_t kFormatRgb565 = 1;
constexpr uint16_t kPreviewWidth = 40;
constexpr uint16_t kPreviewHeight = 30;

constexpr uint8_t kChunkFlagStart = 0x01;
constexpr uint8_t kChunkFlagEnd = 0x02;
constexpr uint8_t kChunkFlagAbort = 0x80;
constexpr size_t kChunkHeaderBytes = 5;
constexpr size_t kChunkStartMetaBytes = 9;
constexpr size_t kChunkMaxBytes = 244;

struct Status {
  uint8_t screen;
  uint8_t modeIndex;
  uint8_t flags;
  uint16_t sdCount;
  uint16_t ramCount;
  uint8_t batteryPct;
};

struct DreamNotice {
  uint8_t state;
  uint8_t modeIndex;
  uint32_t dreamBytes;
  uint16_t thumbWidth;
  uint16_t thumbHeight;
  uint8_t format;
  uint32_t thumbBytes;
  uint16_t generation;
};

struct ModeView {
  const char *id;
  const char *label;
};

struct ChunkCursor {
  size_t offset;
  uint16_t sequence;
  bool finished;
};

struct Chunk {
  uint8_t bytes[kChunkMaxBytes];
  uint16_t length;
};

struct PreviewMeta {
  uint32_t total;
  uint16_t width;
  uint16_t height;
  uint8_t format;
};

size_t packStatus(const Status &status, uint8_t *out, size_t cap);
bool unpackStatus(const uint8_t *in, size_t len, Status &out);

size_t packDream(const DreamNotice &dream, uint8_t *out, size_t cap);
bool unpackDream(const uint8_t *in, size_t len, DreamNotice &out);

// selected, then count, then each mode: index, id, label.
size_t packModes(uint8_t selected, const ModeView *modes, uint8_t count,
                 uint8_t *out, size_t cap);

// One byte is a mode index. Longer writes are the style id (ASCII, no NUL).
int parseModeSelect(const uint8_t *data, size_t len, const ModeView *modes,
                    uint8_t count);
bool parseShutter(const uint8_t *data, size_t len);

void resetCursor(ChunkCursor &cursor);

// One notification. attMtu is the negotiated ATT MTU (payload is mtu - 3).
// The first chunk carries start meta; the chunk that finishes the image
// sets the end flag. Returns false when there is nothing left to send.
bool nextPreviewChunk(const uint8_t *image, size_t imageLen, uint16_t width,
                      uint16_t height, uint8_t format, uint16_t attMtu,
                      ChunkCursor &cursor, Chunk &out);

bool abortPreviewChunk(uint16_t sequence, uint16_t attMtu, Chunk &out);

bool readChunk(const uint8_t *frame, size_t frameLen, uint8_t &flags,
               uint16_t &sequence, const uint8_t *&payload, uint16_t &payloadLen);
bool readStartMeta(const uint8_t *payload, uint16_t payloadLen, PreviewMeta &meta,
                   const uint8_t *&image, uint16_t &imageLen);

}  // namespace BleProto
}  // namespace Somnium
