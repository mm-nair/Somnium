import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PROTO = ROOT / "lib" / "Somnium" / "BleProto.cpp"
HEADER = ROOT / "lib" / "Somnium" / "BleProto.h"

HARNESS = r"""
#include "BleProto.h"

#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

using Somnium::BleProto::Chunk;
using Somnium::BleProto::ChunkCursor;
using Somnium::BleProto::DreamNotice;
using Somnium::BleProto::ModeView;
using Somnium::BleProto::PreviewMeta;
using Somnium::BleProto::Status;

int failures = 0;

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "FAIL " << __LINE__ << " " << #cond << "\n";             \
      ++failures;                                                            \
    }                                                                        \
  } while (0)

std::string hexOf(const uint8_t *data, size_t len) {
  static const char *kDigits = "0123456789abcdef";
  std::string out;
  out.resize(len * 2);
  for (size_t i = 0; i < len; ++i) {
    out[i * 2] = kDigits[data[i] >> 4];
    out[i * 2 + 1] = kDigits[data[i] & 0x0f];
  }
  return out;
}

void testStatus() {
  Status status{};
  status.screen = Somnium::BleProto::kScreenProcess;
  status.modeIndex = 2;
  status.flags = static_cast<uint8_t>(Somnium::BleProto::kFlagBusy |
                                      Somnium::BleProto::kFlagCamera |
                                      Somnium::BleProto::kFlagSd);
  status.sdCount = 0x1234;
  status.ramCount = 7;
  status.batteryPct = Somnium::BleProto::kBatteryAbsent;
  uint8_t packed[16];
  CHECK(Somnium::BleProto::packStatus(status, packed, sizeof(packed)) ==
        Somnium::BleProto::kStatusBytes);
  CHECK(Somnium::BleProto::kStatusBytes <= 20);
  Status back{};
  CHECK(Somnium::BleProto::unpackStatus(packed, Somnium::BleProto::kStatusBytes,
                                        back));
  CHECK(back.screen == status.screen);
  CHECK(back.modeIndex == 2);
  CHECK(back.flags == status.flags);
  CHECK(back.sdCount == 0x1234);
  CHECK(back.ramCount == 7);
  CHECK(back.batteryPct == 0xFF);
  CHECK(packed[0] == 1);
  CHECK(packed[9] == 0);
  CHECK(!Somnium::BleProto::unpackStatus(packed, 9, back));
  packed[0] = 2;
  CHECK(!Somnium::BleProto::unpackStatus(packed, Somnium::BleProto::kStatusBytes,
                                         back));
}

void testDream() {
  DreamNotice dream{};
  dream.state = Somnium::BleProto::kDreamReady;
  dream.modeIndex = 4;
  dream.dreamBytes = 32000;
  dream.thumbWidth = Somnium::BleProto::kPreviewWidth;
  dream.thumbHeight = Somnium::BleProto::kPreviewHeight;
  dream.format = Somnium::BleProto::kFormatRgb565;
  dream.thumbBytes = 40u * 30u * 2u;
  dream.generation = 0x0201;
  uint8_t packed[32];
  CHECK(Somnium::BleProto::packDream(dream, packed, sizeof(packed)) ==
        Somnium::BleProto::kDreamBytes);
  CHECK(Somnium::BleProto::kDreamBytes <= 20);
  DreamNotice back{};
  CHECK(Somnium::BleProto::unpackDream(packed, Somnium::BleProto::kDreamBytes,
                                       back));
  CHECK(back.state == dream.state);
  CHECK(back.modeIndex == 4);
  CHECK(back.dreamBytes == 32000);
  CHECK(back.thumbWidth == 40);
  CHECK(back.thumbHeight == 30);
  CHECK(back.format == 1);
  CHECK(back.thumbBytes == 2400);
  CHECK(back.generation == 0x0201);
}

void testModes() {
  const ModeView modes[] = {
      {"popcomic", "COMIC"},
      {"watercolor", "WATERCOLOR"},
      {"anime", "ANIME"},
      {"surprise", "SURPRISE"},
      {"pencil", "PENCIL SKETCH"},
  };
  uint8_t packed[200];
  const size_t n =
      Somnium::BleProto::packModes(2, modes, 5, packed, sizeof(packed));
  CHECK(n > 2);
  CHECK(packed[0] == 2);
  CHECK(packed[1] == 5);
  CHECK(Somnium::BleProto::parseModeSelect(
            reinterpret_cast<const uint8_t *>("anime"), 5, modes, 5) == 2);
  CHECK(Somnium::BleProto::parseModeSelect(
            reinterpret_cast<const uint8_t *>("watercolor"), 10, modes, 5) ==
        1);
  const uint8_t index = 4;
  CHECK(Somnium::BleProto::parseModeSelect(&index, 1, modes, 5) == 4);
  const uint8_t bad = 5;
  CHECK(Somnium::BleProto::parseModeSelect(&bad, 1, modes, 5) == -1);
  CHECK(Somnium::BleProto::parseModeSelect(
            reinterpret_cast<const uint8_t *>("ANIME"), 5, modes, 5) == -1);
  CHECK(Somnium::BleProto::parseModeSelect(nullptr, 0, modes, 5) == -1);
  CHECK(Somnium::BleProto::parseShutter(
      reinterpret_cast<const uint8_t *>("\x01"), 1));
  CHECK(!Somnium::BleProto::parseShutter(
      reinterpret_cast<const uint8_t *>("\x00"), 1));
  CHECK(!Somnium::BleProto::parseShutter(nullptr, 0));
  CHECK(Somnium::BleProto::packModes(0, modes, 5, packed, 8) == 0);
}

bool reassemble(const std::vector<uint8_t> &image, uint16_t width,
                uint16_t height, uint16_t mtu) {
  ChunkCursor cursor{};
  Somnium::BleProto::resetCursor(cursor);
  std::vector<uint8_t> got;
  PreviewMeta meta{};
  bool haveMeta = false;
  int chunks = 0;
  bool ended = false;
  while (true) {
    Chunk chunk{};
    if (!Somnium::BleProto::nextPreviewChunk(image.data(), image.size(), width,
                                             height, 1, mtu, cursor, chunk)) {
      break;
    }
    CHECK(chunk.length <= static_cast<uint16_t>(mtu - 3));
    CHECK(chunk.length <= Somnium::BleProto::kChunkMaxBytes);
    uint8_t flags = 0;
    uint16_t sequence = 0;
    const uint8_t *payload = nullptr;
    uint16_t payloadLen = 0;
    if (!Somnium::BleProto::readChunk(chunk.bytes, chunk.length, flags, sequence,
                                      payload, payloadLen)) {
      return false;
    }
    CHECK(sequence == chunks);
    if (flags & Somnium::BleProto::kChunkFlagStart) {
      CHECK(chunks == 0);
      const uint8_t *piece = nullptr;
      uint16_t pieceLen = 0;
      if (!Somnium::BleProto::readStartMeta(payload, payloadLen, meta, piece,
                                            pieceLen)) {
        return false;
      }
      haveMeta = true;
      got.insert(got.end(), piece, piece + pieceLen);
    } else {
      CHECK((flags & Somnium::BleProto::kChunkFlagStart) == 0);
      got.insert(got.end(), payload, payload + payloadLen);
    }
    if (chunks > 0) {
      CHECK((flags & Somnium::BleProto::kChunkFlagStart) == 0);
    }
    ended = (flags & Somnium::BleProto::kChunkFlagEnd) != 0;
    if (!ended) {
      CHECK((flags & Somnium::BleProto::kChunkFlagEnd) == 0);
    }
    ++chunks;
  }
  CHECK(haveMeta);
  CHECK(meta.total == image.size());
  CHECK(meta.width == width);
  CHECK(meta.height == height);
  CHECK(meta.format == 1);
  CHECK(ended);
  CHECK(cursor.finished);
  CHECK(got == image);
  CHECK(chunks > 0);
  Chunk extra{};
  CHECK(!Somnium::BleProto::nextPreviewChunk(image.data(), image.size(), width,
                                             height, 1, mtu, cursor, extra));
  return failures == 0;
}

void testChunks() {
  std::vector<uint8_t> tiny{1, 2, 3, 4};
  CHECK(reassemble(tiny, 2, 1, 185));
  std::vector<uint8_t> wide(2400);
  for (size_t i = 0; i < wide.size(); ++i) {
    wide[i] = static_cast<uint8_t>(i * 17u);
  }
  const int before = failures;
  CHECK(reassemble(wide, 40, 30, 23));
  CHECK(reassemble(wide, 40, 30, 185));
  CHECK(reassemble(wide, 40, 30, 247));
  ChunkCursor cursor{};
  Somnium::BleProto::resetCursor(cursor);
  Chunk chunk{};
  CHECK(!Somnium::BleProto::nextPreviewChunk(nullptr, 4, 1, 1, 1, 185, cursor,
                                             chunk));
  CHECK(!Somnium::BleProto::nextPreviewChunk(wide.data(), 0, 1, 1, 1, 185,
                                             cursor, chunk));
  CHECK(!Somnium::BleProto::nextPreviewChunk(wide.data(), wide.size(), 40, 30,
                                             1, 22, cursor, chunk));
  CHECK(cursor.offset == 0);
  CHECK(cursor.sequence == 0);
  CHECK(Somnium::BleProto::abortPreviewChunk(9, 23, chunk));
  CHECK(chunk.length == 5);
  CHECK(chunk.bytes[0] == Somnium::BleProto::kChunkFlagAbort);
  CHECK(chunk.bytes[1] == 9);
  CHECK(chunk.bytes[3] == 0);
  uint8_t flags = 0;
  uint16_t sequence = 0;
  const uint8_t *payload = nullptr;
  uint16_t payloadLen = 1;
  CHECK(Somnium::BleProto::readChunk(chunk.bytes, chunk.length, flags, sequence,
                                     payload, payloadLen));
  CHECK(flags == Somnium::BleProto::kChunkFlagAbort);
  CHECK(sequence == 9);
  CHECK(payloadLen == 0);
  CHECK(failures == before || failures > before);
}

void dump() {
  Status status{};
  status.screen = Somnium::BleProto::kScreenReview;
  status.modeIndex = 2;
  status.flags = static_cast<uint8_t>(Somnium::BleProto::kFlagDream |
                                      Somnium::BleProto::kFlagCamera |
                                      Somnium::BleProto::kFlagSd);
  status.sdCount = 3;
  status.ramCount = 1;
  status.batteryPct = Somnium::BleProto::kBatteryAbsent;
  uint8_t statusPack[16];
  const size_t statusN = Somnium::BleProto::packStatus(
      status, statusPack, sizeof(statusPack));
  std::cout << "STATUS " << hexOf(statusPack, statusN) << "\n";

  DreamNotice dream{};
  dream.state = Somnium::BleProto::kDreamReady;
  dream.modeIndex = 2;
  dream.dreamBytes = 18000;
  dream.thumbWidth = 40;
  dream.thumbHeight = 30;
  dream.format = 1;
  dream.thumbBytes = 80;
  dream.generation = 4;
  uint8_t dreamPack[32];
  const size_t dreamN =
      Somnium::BleProto::packDream(dream, dreamPack, sizeof(dreamPack));
  std::cout << "DREAM " << hexOf(dreamPack, dreamN) << "\n";

  const ModeView modes[] = {
      {"popcomic", "COMIC"}, {"watercolor", "WATERCOLOR"},
      {"anime", "ANIME"},    {"surprise", "SURPRISE"},
      {"pencil", "PENCIL SKETCH"},
  };
  uint8_t modePack[200];
  const size_t modeN =
      Somnium::BleProto::packModes(2, modes, 5, modePack, sizeof(modePack));
  std::cout << "MODES " << hexOf(modePack, modeN) << "\n";

  std::vector<uint8_t> image(80);
  for (size_t i = 0; i < image.size(); ++i) {
    image[i] = static_cast<uint8_t>(i + 1);
  }
  ChunkCursor cursor{};
  Somnium::BleProto::resetCursor(cursor);
  while (true) {
    Chunk chunk{};
    if (!Somnium::BleProto::nextPreviewChunk(image.data(), image.size(), 20, 2,
                                             1, 23, cursor, chunk)) {
      break;
    }
    std::cout << "CHUNK " << hexOf(chunk.bytes, chunk.length) << "\n";
  }
  Chunk abort{};
  Somnium::BleProto::abortPreviewChunk(3, 23, abort);
  std::cout << "ABORT " << hexOf(abort.bytes, abort.length) << "\n";
  std::cout << "END\n";
}

int main(int argc, char **argv) {
  if (argc == 2 && std::strcmp(argv[1], "dump") == 0) {
    dump();
    return 0;
  }
  testStatus();
  testDream();
  testModes();
  testChunks();
  if (failures != 0) {
    std::cerr << failures << " checks failed\n";
    return 1;
  }
  return 0;
}
"""


class BleFramingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if shutil.which("g++") is None:
            raise RuntimeError("Run tests inside nix develop")
        cls.temporary_directory = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temporary_directory.cleanup)
        directory = Path(cls.temporary_directory.name)
        harness = directory / "ble_framing.cpp"
        harness.write_text(HARNESS)
        cls.executable = directory / "ble_framing"
        subprocess.run(
            [
                "g++",
                "-std=c++11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I",
                str(ROOT / "lib" / "Somnium"),
                str(harness),
                str(PROTO),
                "-o",
                str(cls.executable),
            ],
            check=True,
            text=True,
        )

    def test_protocol_roundtrip(self):
        result = subprocess.run(
            [str(self.executable)], capture_output=True, text=True
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_python_codec_matches_firmware_dump(self):
        import sys

        sys.path.insert(0, str(ROOT / "tools" / "ble"))
        from framing import (  # noqa: E402
            PreviewAssembler,
            parse_chunk,
            unpack_dream,
            unpack_modes,
            unpack_status,
        )

        result = subprocess.run(
            [str(self.executable), "dump"], capture_output=True, text=True, check=True
        )
        records = {}
        chunks = []
        for line in result.stdout.splitlines():
            if not line or line == "END" or " " not in line:
                continue
            kind, hex_bytes = line.split(" ", 1)
            raw = bytes.fromhex(hex_bytes)
            if kind == "CHUNK":
                chunks.append(raw)
            else:
                records[kind] = raw

        status = unpack_status(records["STATUS"])
        self.assertEqual(status["screen_name"], "review")
        self.assertEqual(status["mode_index"], 2)
        self.assertTrue(status["dream_ready"])
        self.assertTrue(status["camera_ok"])
        self.assertTrue(status["sd_ready"])
        self.assertEqual(status["sd_count"], 3)
        self.assertEqual(status["ram_count"], 1)
        self.assertIsNone(status["battery_pct"])

        dream = unpack_dream(records["DREAM"])
        self.assertEqual(dream["state_name"], "ready")
        self.assertEqual(dream["dream_bytes"], 18000)
        self.assertEqual(dream["thumb_width"], 40)
        self.assertEqual(dream["thumb_height"], 30)
        self.assertEqual(dream["generation"], 4)
        self.assertEqual(dream["thumb_bytes"], 80)

        modes = unpack_modes(records["MODES"])
        self.assertEqual(modes["selected"], 2)
        self.assertEqual(
            [mode["id"] for mode in modes["modes"]],
            ["popcomic", "watercolor", "anime", "surprise", "pencil"],
        )
        self.assertEqual(modes["modes"][4]["label"], "PENCIL SKETCH")

        assembler = PreviewAssembler()
        image = None
        for chunk in chunks:
            image = assembler.push(chunk) or image
        self.assertIsNotNone(image)
        self.assertEqual(image, bytes(range(1, 81)))
        abort = parse_chunk(records["ABORT"])
        self.assertTrue(abort["abort"])
        self.assertEqual(abort["sequence"], 3)
        self.assertEqual(abort["image"], b"")

    def test_public_uuids_and_mode_ids_stay_aligned(self):
        header = HEADER.read_text()
        html = (ROOT / "tools" / "ble" / "companion.html").read_text()
        framing = (ROOT / "tools" / "ble" / "framing.py").read_text()
        readme = (ROOT / "tools" / "ble" / "README.md").read_text()
        for uuid in (
            "534f4d4e-1000-4000-8000-534f4d4e494d",
            "534f4d4e-1001-4000-8000-534f4d4e494d",
            "534f4d4e-1002-4000-8000-534f4d4e494d",
            "534f4d4e-1003-4000-8000-534f4d4e494d",
            "534f4d4e-1004-4000-8000-534f4d4e494d",
            "534f4d4e-1005-4000-8000-534f4d4e494d",
            "534f4d4e-1006-4000-8000-534f4d4e494d",
        ):
            self.assertIn(uuid, header)
            self.assertIn(uuid, html)
            self.assertIn(uuid, framing)
            self.assertIn(uuid, readme)
        modes = (ROOT / "lib" / "SomniumApp" / "Modes.h").read_text()
        for style_id, label in (
            ("popcomic", "COMIC"),
            ("watercolor", "WATERCOLOR"),
            ("anime", "ANIME"),
            ("surprise", "SURPRISE"),
            ("pencil", "PENCIL SKETCH"),
        ):
            self.assertIn(f'"{style_id}"', modes)
            self.assertIn(f'"{label}"', modes)
            self.assertIn(style_id, html)
            self.assertIn(label, html)


if __name__ == "__main__":
    unittest.main()
