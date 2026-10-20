import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]

HARNESS = r"""
#include "HttpFormat.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <string>

struct Buf {
  std::string data;
  int writes = 0;
};

bool collect(void *ctx, const uint8_t *data, size_t len) {
  Buf *buf = static_cast<Buf *>(ctx);
  buf->writes += 1;
  buf->data.append(reinterpret_cast<const char *>(data), len);
  return true;
}

std::string headerValue(const std::string &raw, const std::string &name) {
  const std::string key = name + ": ";
  const size_t line = raw.find(key);
  assert(line != std::string::npos);
  const size_t end = raw.find("\r\n", line);
  assert(end != std::string::npos);
  return raw.substr(line + key.size(), end - (line + key.size()));
}

void check(const char *name) { std::cout << name << "\n"; }

int main() {
  const uint8_t jpeg[] = "----SomniumCook00000000\xff\xd8\xff\xd9";
  const size_t jpegLen = sizeof(jpeg) - 1;
  Somnium::HttpPart style{"style", nullptr, nullptr,
                          reinterpret_cast<const uint8_t *>("anime"), 5};
  Somnium::HttpPart image{"image", "capture.jpg", "image/jpeg", jpeg, jpegLen};

  {
    check("open server omits the token and keeps a non-default port");
    Buf buf;
    Somnium::HttpPart parts[] = {style, image};
    Somnium::HttpTarget target{"somnium.local", 8080, 0, "/v1/process", nullptr};
    assert(Somnium::httpWriteRequest(target, parts, 2, collect, &buf));
    assert(buf.data.find("X-Somnium-Token:") == std::string::npos);
    assert(headerValue(buf.data, "Host") == "somnium.local:8080");
    const std::string length = headerValue(buf.data, "Content-Length");
    const size_t split = buf.data.find("\r\n\r\n");
    assert(split != std::string::npos);
    const std::string body = buf.data.substr(split + 4);
    assert(std::to_string(body.size()) == length);
    assert(body.find("name=\"skip_gallery\"") == std::string::npos);
    assert(body.find("\xff\xd8\xff\xd9") != std::string::npos);
  }

  {
    check("tls 443 host omits the port and sends the token once");
    Buf buf;
    Somnium::HttpPart skip{"skip_gallery", nullptr, nullptr,
                           reinterpret_cast<const uint8_t *>("1"), 1};
    Somnium::HttpPart parts[] = {style, skip, image};
    Somnium::HttpTarget target{"somnium.example", 443, 1, "/v1/process", "s3cret"};
    assert(Somnium::httpWriteRequest(target, parts, 3, collect, &buf));
    assert(headerValue(buf.data, "Host") == "somnium.example");
    assert(headerValue(buf.data, "X-Somnium-Token") == "s3cret");
    assert(buf.data.find("X-Somnium-Token:") == buf.data.rfind("X-Somnium-Token:"));
    const size_t split = buf.data.find("\r\n\r\n");
    const std::string body = buf.data.substr(split + 4);
    assert(std::to_string(body.size()) == headerValue(buf.data, "Content-Length"));
    assert(body.find("name=\"skip_gallery\"") != std::string::npos);
    assert(body.find("s3cret") == std::string::npos);
    const std::string boundary = headerValue(buf.data, "Content-Type");
    assert(boundary.find("----SomniumCook00000000") == std::string::npos);
    const size_t jpegAt = body.find(reinterpret_cast<const char *>(jpeg), 0, jpegLen);
    assert(jpegAt != std::string::npos);
    assert(body.find(reinterpret_cast<const char *>(jpeg), jpegAt + 1, jpegLen) ==
           std::string::npos);
  }

  {
    check("plain 443 keeps the port and an empty token adds no header");
    Buf buf;
    Somnium::HttpPart parts[] = {style, image};
    Somnium::HttpTarget target{"somnium.example", 443, 0, "/v1/process", ""};
    assert(Somnium::httpWriteRequest(target, parts, 2, collect, &buf));
    assert(headerValue(buf.data, "Host") == "somnium.example:443");
    assert(buf.data.find("X-Somnium-Token:") == std::string::npos);
  }

  {
    check("a token with a newline is refused before any write");
    Buf buf;
    Somnium::HttpPart parts[] = {style, image};
    Somnium::HttpTarget target{"somnium.example", 443, 1, "/v1/process", "bad\ntoken"};
    assert(!Somnium::httpTokenHeaderSafe("bad\ntoken"));
    assert(!Somnium::httpTokenHeaderSafe(""));
    assert(!Somnium::httpTokenHeaderSafe(nullptr));
    assert(Somnium::httpTokenHeaderSafe("s3cret"));
    assert(!Somnium::httpWriteRequest(target, parts, 2, collect, &buf));
    assert(buf.writes == 0);
    assert(buf.data.empty());
  }
  return 0;
}
"""


class HttpFormatTests(unittest.TestCase):
    def test_request_shape(self):
        if shutil.which("g++") is None:
            self.fail("g++ is required")
        with tempfile.TemporaryDirectory() as directory:
            harness = Path(directory) / "http_format_main.cpp"
            harness.write_text(HARNESS)
            executable = Path(directory) / "http_format"
            compiled = subprocess.run(
                [
                    "g++",
                    "-std=c++11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT / "lib/Somnium"),
                    str(harness),
                    str(ROOT / "lib/Somnium/HttpFormat.cpp"),
                    "-o",
                    str(executable),
                ],
                capture_output=True,
                text=True,
            )
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            ran = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)


if __name__ == "__main__":
    unittest.main()
