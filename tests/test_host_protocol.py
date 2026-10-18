import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]

ARDUINO = r"""
#pragma once
#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <string>
using String = std::string;
using SemaphoreHandle_t = void *;
extern unsigned long clockMs;
inline unsigned long millis() { return clockMs++; }
inline void yield() { ++clockMs; }
inline void delay(unsigned long value) { clockMs += value; }
inline void *ps_malloc(size_t size) { return malloc(size); }
class FakeSerial {
 public:
  std::deque<uint8_t> incoming;
  std::string outgoing;
  std::function<void()> onFlush;
  std::function<void()> onAvailable;
  int available() {
    if (onAvailable) { onAvailable(); }
    return std::min<size_t>(incoming.size(), 3);
  }
  int read() {
    if (incoming.empty()) { return -1; }
    int value = incoming.front();
    incoming.pop_front();
    return value;
  }
  size_t readBytes(uint8_t *buffer, size_t length) {
    size_t count = 0;
    while (count < length && !incoming.empty()) {
      buffer[count++] = read();
    }
    return count;
  }
  size_t write(const uint8_t *data, size_t length) {
    outgoing.append(reinterpret_cast<const char *>(data), length);
    return length;
  }
  void print(const char *value) { outgoing += value; }
  void printf(const char *format, ...) {
    char buffer[512];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    outgoing += buffer;
  }
  void flush() { if (onFlush) { onFlush(); } }
  void receive(const std::string &value) {
    incoming.insert(incoming.end(), value.begin(), value.end());
  }
};
extern FakeSerial Serial;
"""

HARNESS = r"""
#include "Host.h"
#include "Image.h"
#include "Console.h"
#include <esp_rom_crc.h>
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

unsigned long clockMs = 0;
FakeSerial Serial;
unsigned currentRequestId = 0x1234abcd;
std::vector<unsigned long> testPumpTimes;
bool testBinaryStarted = false;

void recordPump() {
  assert(!testBinaryStarted);
  testPumpTimes.push_back(clockMs);
  clockMs += 20;
}

uint32_t esp_random() { return currentRequestId++; }

uint32_t esp_rom_crc32_le(uint32_t crc, const uint8_t *data, uint32_t length) {
  crc = ~crc;
  for (uint32_t index = 0; index < length; ++index) {
    crc ^= data[index];
    for (unsigned bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
  }
  return ~crc;
}

namespace Somnium {
bool Image::looksLikeJpeg(const uint8_t *data, size_t length) {
  return length >= 4 && data[0] == 0xff && data[1] == 0xd8 &&
         data[length - 2] == 0xff && data[length - 1] == 0xd9;
}
void Console::printf(const char *format, ...) {
  char buffer[512];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  Serial.print(buffer);
}
}

void verifyTransfer(const std::string &scenario) {
  Serial.incoming.clear();
  Serial.outgoing.clear();
  const unsigned requestId = currentRequestId;
  std::string jpeg("\xff\xd8" "example" "\xff\xd9", 11);
  if (scenario == "bad_jpeg") { jpeg[0] = 0; }
  const unsigned checksum = esp_rom_crc32_le(
      0, reinterpret_cast<const uint8_t *>(jpeg.data()), jpeg.size());
  char header[128];
  snprintf(header, sizeof(header), "DREAM %08x %u %08x\n", requestId,
           static_cast<unsigned>(jpeg.size()), checksum);
  std::string responseHeader = header;
  if (scenario == "stale") {
    responseHeader = "DREAM ffffffff 11 00000000\n" + responseHeader;
  } else if (scenario == "legacy") {
    responseHeader = "DREAM 11\n";
  } else if (scenario == "oversize") {
    snprintf(header, sizeof(header), "DREAM %08x 400001 %08x\n", requestId,
             checksum);
    responseHeader = header;
  } else if (scenario == "remote_error") {
    snprintf(header, sizeof(header), "ERR %08x backend failed\n", requestId);
    responseHeader = header;
  } else if (scenario == "legacy_error") {
    responseHeader = "ERR bridge protocol mismatch\n";
  } else if (scenario == "stale_error") {
    responseHeader = "ERR ffffffff backend failed\n" + responseHeader;
  } else if (scenario == "overflow") {
    responseHeader = std::string(150, 'x') + "\n" + responseHeader;
  }
  std::string payload = jpeg + "\nDREAM_END\n";
  if (scenario == "corrupt") { payload[4] ^= 1; }
  if (scenario == "trailer") { payload.back() = 'x'; }
  if (scenario == "short") { payload = jpeg.substr(0, 4); }
  if (scenario == "missing_trailer") { payload = jpeg; }
  bool headerSent = false;
  bool payloadSent = false;
  Serial.onFlush = [&] {
    if (!headerSent && Serial.outgoing.find("\nCOOK_END\n") != std::string::npos) {
      headerSent = true;
      Serial.receive(responseHeader);
    }
    if (!payloadSent && Serial.outgoing.find("READY DREAM ") != std::string::npos) {
      payloadSent = true;
      Serial.receive(payload);
    }
  };
  Somnium::JpegBuffer dream;
  String error;
  const bool ok = Somnium::Host::cook(
      reinterpret_cast<const uint8_t *>(jpeg.data()), jpeg.size(), "anime",
      nullptr, NAN, NAN, dream, &error);
  const bool expected = scenario == "valid" || scenario == "stale" ||
                        scenario == "stale_error" || scenario == "overflow";
  if (ok != expected) {
    std::cerr << scenario << ": " << error << "\n" << Serial.outgoing;
    std::abort();
  }
  assert(headerSent);
  assert(Serial.outgoing.find("COOK style=anime ") != std::string::npos);
  assert(Serial.outgoing.find(" device=") == std::string::npos);
  assert(Serial.outgoing.find(" board=") == std::string::npos);
  assert((Serial.outgoing.find("ACK DREAM ") != std::string::npos) == expected);
  if (expected) {
    assert(payloadSent);
    assert(dream.length == jpeg.size());
    assert(dream.crc32 == checksum);
    assert(memcmp(dream.data, jpeg.data(), jpeg.size()) == 0);
    assert(Serial.incoming.empty());
    free(dream.data);
  } else {
    assert(dream.data == nullptr);
    assert(dream.length == 0);
    assert(!error.empty());
    if (scenario == "legacy" || scenario == "oversize" ||
        scenario == "remote_error" || scenario == "legacy_error") {
      assert(!payloadSent);
    }
    if (scenario == "legacy_error") {
      assert(error == "bridge protocol mismatch");
    }
    if (scenario == "corrupt") { assert(error == "dream crc mismatch"); }
    if (scenario == "trailer" || scenario == "missing_trailer") {
      assert(error == "dream trailer");
    }
  }
  Serial.onFlush = nullptr;
}

void verifyAnimationCadence() {
  Serial.incoming.clear();
  Serial.outgoing.clear();
  testPumpTimes.clear();
  testBinaryStarted = false;
  const unsigned requestId = currentRequestId;
  const std::string jpeg("\xff\xd8" "example" "\xff\xd9", 11);
  const unsigned checksum = esp_rom_crc32_le(
      0, reinterpret_cast<const uint8_t *>(jpeg.data()), jpeg.size());
  char header[128];
  snprintf(header, sizeof(header), "DREAM %08x %u %08x\n", requestId,
           static_cast<unsigned>(jpeg.size()), checksum);
  bool cookSent = false;
  bool headerSent = false;
  bool payloadSent = false;
  bool trailerSent = false;
  unsigned long cookSentAt = 0;
  unsigned long binaryStartedAt = 0;
  Serial.onFlush = [&] {
    if (!cookSent && Serial.outgoing.find("\nCOOK_END\n") != std::string::npos) {
      cookSent = true;
      cookSentAt = clockMs;
    }
    if (!testBinaryStarted &&
        Serial.outgoing.find("READY DREAM ") != std::string::npos) {
      testBinaryStarted = true;
      binaryStartedAt = clockMs;
    }
  };
  Serial.onAvailable = [&] {
    if (cookSent && !headerSent && clockMs - cookSentAt >= 750) {
      headerSent = true;
      Serial.receive(header);
    }
    if (testBinaryStarted && !payloadSent && clockMs - binaryStartedAt >= 300) {
      payloadSent = true;
      Serial.receive(jpeg);
    }
    if (testBinaryStarted && !trailerSent && clockMs - binaryStartedAt >= 600) {
      trailerSent = true;
      Serial.receive("\nDREAM_END\n");
    }
  };
  Somnium::Host::setPump(recordPump);
  Somnium::JpegBuffer dream;
  String error;
  assert(Somnium::Host::cook(
      reinterpret_cast<const uint8_t *>(jpeg.data()), jpeg.size(), "anime",
      nullptr, NAN, NAN, dream, &error));
  assert(headerSent && payloadSent && trailerSent);
  assert(testPumpTimes.size() >= 6 && testPumpTimes.size() <= 8);
  unsigned long previousPump = cookSentAt;
  for (unsigned long pumpTime : testPumpTimes) {
    assert(pumpTime - previousPump >= 100);
    assert(pumpTime - previousPump <= 105);
    previousPump = pumpTime;
  }
  assert(dream.length == jpeg.size());
  assert(memcmp(dream.data, jpeg.data(), jpeg.size()) == 0);
  assert(Serial.outgoing.find("ACK DREAM ") != std::string::npos);
  free(dream.data);
  Somnium::Host::setPump(nullptr);
  Serial.onFlush = nullptr;
  Serial.onAvailable = nullptr;
}

int main(int argc, char **argv) {
  assert(argc == 2);
  const uint8_t reference[] = "123456789";
  assert(esp_rom_crc32_le(0, reference, 9) == 0xcbf43926);
  if (strcmp(argv[1], "animation_cadence") == 0) {
    verifyAnimationCadence();
  } else {
    verifyTransfer(argv[1]);
  }
  verifyTransfer("valid");
}
"""


class HostProtocolTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if shutil.which("g++") is None:
            raise RuntimeError("Run tests inside nix develop")
        cls.temporary_directory = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temporary_directory.cleanup)
        directory = Path(cls.temporary_directory.name)
        (directory / "Arduino.h").write_text(ARDUINO)
        (directory / "Adafruit_GFX.h").write_text("class Adafruit_GFX {};\n")
        (directory / "esp_camera.h").write_text("struct camera_fb_t {};\n")
        (directory / "esp_system.h").write_text(
            "#include <stdint.h>\nuint32_t esp_random();\n"
        )
        (directory / "esp_rom_crc.h").write_text(
            "#include <stdint.h>\n"
            "uint32_t esp_rom_crc32_le(uint32_t, const uint8_t *, uint32_t);\n"
        )
        harness = directory / "host_protocol.cpp"
        harness.write_text(HARNESS)
        cls.executable = directory / "host_protocol"
        subprocess.run(
            [
                "g++",
                "-std=c++11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-ffunction-sections",
                "-fdata-sections",
                "-Wl,--gc-sections",
                "-I",
                str(directory),
                "-I",
                str(ROOT / "lib/Somnium"),
                str(harness),
                str(ROOT / "lib/Somnium/Host.cpp"),
                "-o",
                str(cls.executable),
            ],
            check=True,
            text=True,
        )

    def test_transfer_validation_and_next_capture(self):
        for scenario in (
            "valid",
            "stale",
            "stale_error",
            "overflow",
            "legacy",
            "oversize",
            "remote_error",
            "legacy_error",
            "corrupt",
            "trailer",
            "short",
            "missing_trailer",
            "bad_jpeg",
        ):
            with self.subTest(scenario=scenario):
                result = subprocess.run(
                    [str(self.executable), scenario], capture_output=True, text=True
                )
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_idle_animation_cadence_keeps_binary_transfer_exclusive(self):
        result = subprocess.run(
            [str(self.executable), "animation_cadence"],
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
