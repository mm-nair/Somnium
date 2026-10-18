#include "BleProto.h"

#include <string.h>

namespace Somnium {
namespace BleProto {
namespace {

void writeU16(uint8_t *out, uint16_t value) {
  out[0] = static_cast<uint8_t>(value);
  out[1] = static_cast<uint8_t>(value >> 8);
}

void writeU32(uint8_t *out, uint32_t value) {
  out[0] = static_cast<uint8_t>(value);
  out[1] = static_cast<uint8_t>(value >> 8);
  out[2] = static_cast<uint8_t>(value >> 16);
  out[3] = static_cast<uint8_t>(value >> 24);
}

uint16_t readU16(const uint8_t *in) {
  return static_cast<uint16_t>(in[0] | (static_cast<uint16_t>(in[1]) << 8));
}

uint32_t readU32(const uint8_t *in) {
  return static_cast<uint32_t>(in[0]) |
         (static_cast<uint32_t>(in[1]) << 8) |
         (static_cast<uint32_t>(in[2]) << 16) |
         (static_cast<uint32_t>(in[3]) << 24);
}

bool sameText(const char *text, const uint8_t *bytes, size_t len) {
  if (text == nullptr) {
    return false;
  }
  size_t n = 0;
  while (text[n] != '\0') {
    ++n;
  }
  if (n != len) {
    return false;
  }
  return memcmp(text, bytes, len) == 0;
}

}  // namespace

size_t packStatus(const Status &status, uint8_t *out, size_t cap) {
  if (out == nullptr || cap < kStatusBytes) {
    return 0;
  }
  memset(out, 0, kStatusBytes);
  out[0] = kStatusVersion;
  out[1] = status.screen;
  out[2] = status.modeIndex;
  out[3] = status.flags;
  writeU16(out + 4, status.sdCount);
  writeU16(out + 6, status.ramCount);
  out[8] = status.batteryPct;
  return kStatusBytes;
}

bool unpackStatus(const uint8_t *in, size_t len, Status &out) {
  if (in == nullptr || len < kStatusBytes || in[0] != kStatusVersion) {
    return false;
  }
  memset(&out, 0, sizeof(out));
  out.screen = in[1];
  out.modeIndex = in[2];
  out.flags = in[3];
  out.sdCount = readU16(in + 4);
  out.ramCount = readU16(in + 6);
  out.batteryPct = in[8];
  return true;
}

size_t packDream(const DreamNotice &dream, uint8_t *out, size_t cap) {
  if (out == nullptr || cap < kDreamBytes) {
    return 0;
  }
  memset(out, 0, kDreamBytes);
  out[0] = dream.state;
  out[1] = dream.modeIndex;
  writeU32(out + 2, dream.dreamBytes);
  writeU16(out + 6, dream.thumbWidth);
  writeU16(out + 8, dream.thumbHeight);
  out[10] = dream.format;
  writeU32(out + 12, dream.thumbBytes);
  writeU16(out + 16, dream.generation);
  return kDreamBytes;
}

bool unpackDream(const uint8_t *in, size_t len, DreamNotice &out) {
  if (in == nullptr || len < kDreamBytes) {
    return false;
  }
  memset(&out, 0, sizeof(out));
  out.state = in[0];
  out.modeIndex = in[1];
  out.dreamBytes = readU32(in + 2);
  out.thumbWidth = readU16(in + 6);
  out.thumbHeight = readU16(in + 8);
  out.format = in[10];
  out.thumbBytes = readU32(in + 12);
  out.generation = readU16(in + 16);
  return true;
}

size_t packModes(uint8_t selected, const ModeView *modes, uint8_t count,
                 uint8_t *out, size_t cap) {
  if (out == nullptr || (count > 0 && modes == nullptr)) {
    return 0;
  }
  size_t used = 2;
  if (cap < used) {
    return 0;
  }
  out[0] = selected;
  out[1] = count;
  for (uint8_t i = 0; i < count; ++i) {
    const char *id = modes[i].id != nullptr ? modes[i].id : "";
    const char *label = modes[i].label != nullptr ? modes[i].label : "";
    size_t idLen = 0;
    size_t labelLen = 0;
    while (id[idLen] != '\0' && idLen < kNameBytes - 1) {
      ++idLen;
    }
    while (label[labelLen] != '\0' && labelLen < kNameBytes - 1) {
      ++labelLen;
    }
    const size_t need = 1 + 1 + idLen + 1 + labelLen;
    if (used + need > cap) {
      return 0;
    }
    out[used++] = i;
    out[used++] = static_cast<uint8_t>(idLen);
    memcpy(out + used, id, idLen);
    used += idLen;
    out[used++] = static_cast<uint8_t>(labelLen);
    memcpy(out + used, label, labelLen);
    used += labelLen;
  }
  return used;
}

int parseModeSelect(const uint8_t *data, size_t len, const ModeView *modes,
                    uint8_t count) {
  if (data == nullptr || len == 0 || modes == nullptr || count == 0) {
    return -1;
  }
  if (len == 1) {
    if (data[0] >= count) {
      return -1;
    }
    return data[0];
  }
  for (uint8_t i = 0; i < count; ++i) {
    if (sameText(modes[i].id, data, len)) {
      return i;
    }
  }
  return -1;
}

bool parseShutter(const uint8_t *data, size_t len) {
  return data != nullptr && len >= 1 && data[0] != 0;
}

void resetCursor(ChunkCursor &cursor) {
  cursor.offset = 0;
  cursor.sequence = 0;
  cursor.finished = false;
}

bool nextPreviewChunk(const uint8_t *image, size_t imageLen, uint16_t width,
                      uint16_t height, uint8_t format, uint16_t attMtu,
                      ChunkCursor &cursor, Chunk &out) {
  out.length = 0;
  if (image == nullptr || imageLen == 0 || cursor.finished || attMtu < 23 ||
      cursor.offset > imageLen) {
    return false;
  }
  size_t budget = static_cast<size_t>(attMtu) - 3;
  if (budget > kChunkMaxBytes) {
    budget = kChunkMaxBytes;
  }
  if (budget <= kChunkHeaderBytes) {
    return false;
  }
  const size_t room = budget - kChunkHeaderBytes;
  const bool start = cursor.offset == 0;
  if (start && room <= kChunkStartMetaBytes) {
    return false;
  }
  const size_t dataRoom = start ? room - kChunkStartMetaBytes : room;
  if (dataRoom == 0) {
    return false;
  }
  const size_t remain = imageLen - cursor.offset;
  const size_t take = remain < dataRoom ? remain : dataRoom;
  const bool end = take == remain;

  uint8_t flags = 0;
  if (start) {
    flags = static_cast<uint8_t>(flags | kChunkFlagStart);
  }
  if (end) {
    flags = static_cast<uint8_t>(flags | kChunkFlagEnd);
  }
  out.bytes[0] = flags;
  writeU16(out.bytes + 1, cursor.sequence);
  const uint16_t payloadLen =
      static_cast<uint16_t>(take + (start ? kChunkStartMetaBytes : 0));
  writeU16(out.bytes + 3, payloadLen);
  size_t at = kChunkHeaderBytes;
  if (start) {
    writeU32(out.bytes + at, static_cast<uint32_t>(imageLen));
    writeU16(out.bytes + at + 4, width);
    writeU16(out.bytes + at + 6, height);
    out.bytes[at + 8] = format;
    at += kChunkStartMetaBytes;
  }
  memcpy(out.bytes + at, image + cursor.offset, take);
  at += take;
  out.length = static_cast<uint16_t>(at);
  cursor.offset += take;
  cursor.sequence = static_cast<uint16_t>(cursor.sequence + 1);
  cursor.finished = end;
  return true;
}

bool abortPreviewChunk(uint16_t sequence, uint16_t attMtu, Chunk &out) {
  out.length = 0;
  if (attMtu < 23) {
    return false;
  }
  out.bytes[0] = kChunkFlagAbort;
  writeU16(out.bytes + 1, sequence);
  writeU16(out.bytes + 3, 0);
  out.length = kChunkHeaderBytes;
  return true;
}

bool readChunk(const uint8_t *frame, size_t frameLen, uint8_t &flags,
               uint16_t &sequence, const uint8_t *&payload,
               uint16_t &payloadLen) {
  payload = nullptr;
  payloadLen = 0;
  flags = 0;
  sequence = 0;
  if (frame == nullptr || frameLen < kChunkHeaderBytes) {
    return false;
  }
  flags = frame[0];
  sequence = readU16(frame + 1);
  payloadLen = readU16(frame + 3);
  if (kChunkHeaderBytes + payloadLen != frameLen) {
    return false;
  }
  payload = frame + kChunkHeaderBytes;
  return true;
}

bool readStartMeta(const uint8_t *payload, uint16_t payloadLen, PreviewMeta &meta,
                   const uint8_t *&image, uint16_t &imageLen) {
  image = nullptr;
  imageLen = 0;
  if (payload == nullptr || payloadLen < kChunkStartMetaBytes) {
    return false;
  }
  meta.total = readU32(payload);
  meta.width = readU16(payload + 4);
  meta.height = readU16(payload + 6);
  meta.format = payload[8];
  image = payload + kChunkStartMetaBytes;
  imageLen = static_cast<uint16_t>(payloadLen - kChunkStartMetaBytes);
  return true;
}

}  // namespace BleProto
}  // namespace Somnium
