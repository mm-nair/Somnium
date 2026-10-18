#include "ApiClient.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <math.h>

#include "Console.h"
#include "ServerConfig.h"

namespace Somnium {
namespace {

ApiClient::PumpFn gPump = nullptr;

void pump() {
  if (gPump != nullptr) {
    gPump();
  } else {
    yield();
  }
}

String baseUrl() {
  return String("http://") + ServerConfig::kHost + ":" +
         String(ServerConfig::kPort);
}

bool readBinaryBody(HTTPClient &http, uint8_t **outData, size_t *outLen) {
  *outData = nullptr;
  *outLen = 0;
  WiFiClient *stream = http.getStreamPtr();
  if (stream == nullptr) {
    return false;
  }

  const int declared = http.getSize();  // -1 if unknown / chunked
  constexpr size_t kMaxJpeg = 400000;
  constexpr size_t kGrow = 16384;

  if (declared > 0) {
    const size_t need = static_cast<size_t>(declared);
    if (need > kMaxJpeg) {
      return false;
    }
    uint8_t *buf = static_cast<uint8_t *>(ps_malloc(need));
    if (buf == nullptr) {
      buf = static_cast<uint8_t *>(malloc(need));
    }
    if (buf == nullptr) {
      return false;
    }
    size_t got = 0;
    const unsigned long deadline = millis() + ServerConfig::kTimeoutMs;
    while (got < need && millis() < deadline) {
      pump();
      if (stream->available() == 0) {
        if (!stream->connected() && stream->available() == 0) {
          break;
        }
        delay(1);
        continue;
      }
      got += stream->readBytes(buf + got, need - got);
    }
    if (got < 4) {
      free(buf);
      return false;
    }
    *outData = buf;
    *outLen = got;
    return true;
  }

  size_t cap = kGrow;
  uint8_t *buf = static_cast<uint8_t *>(ps_malloc(cap));
  if (buf == nullptr) {
    buf = static_cast<uint8_t *>(malloc(cap));
  }
  if (buf == nullptr) {
    return false;
  }
  size_t got = 0;
  const unsigned long deadline = millis() + ServerConfig::kTimeoutMs;
  while (millis() < deadline) {
    pump();
    const int avail = stream->available();
    if (avail <= 0) {
      if (!stream->connected() && stream->available() == 0) {
        break;
      }
      delay(1);
      continue;
    }
    size_t chunk = static_cast<size_t>(avail);
    if (got + chunk > kMaxJpeg) {
      chunk = kMaxJpeg - got;
    }
    if (chunk == 0) {
      break;
    }
    if (got + chunk > cap) {
      size_t ncap = cap;
      while (ncap < got + chunk) {
        ncap *= 2;
      }
      if (ncap > kMaxJpeg) {
        ncap = kMaxJpeg;
      }
      uint8_t *nbuf = static_cast<uint8_t *>(ps_malloc(ncap));
      if (nbuf == nullptr) {
        nbuf = static_cast<uint8_t *>(malloc(ncap));
      }
      if (nbuf == nullptr) {
        free(buf);
        return false;
      }
      memcpy(nbuf, buf, got);
      free(buf);
      buf = nbuf;
      cap = ncap;
    }
    got += stream->readBytes(buf + got, chunk);
    if (got >= kMaxJpeg) {
      break;
    }
  }
  if (got < 4) {
    free(buf);
    return false;
  }
  *outData = buf;
  *outLen = got;
  return true;
}

bool jpegHasEoi(const uint8_t *data, size_t length) {
  if (data == nullptr || length < 4) {
    return false;
  }
  const size_t scanFrom = length > 32 ? length - 32 : 2;
  for (size_t i = length - 1; i >= scanFrom; --i) {
    if (data[i - 1] == 0xFF && data[i] == 0xD9) {
      return true;
    }
  }
  return false;
}

}  // namespace

void ApiClient::setPump(PumpFn fn) { gPump = fn; }

bool ApiClient::health(String *err) {
  if (WiFi.status() != WL_CONNECTED) {
    if (err != nullptr) {
      *err = "wifi";
    }
    return false;
  }
  HTTPClient http;
  http.setTimeout(8000);
  http.setConnectTimeout(5000);
  const String url = baseUrl() + "/health";
  if (!http.begin(url)) {
    if (err != nullptr) {
      *err = "begin";
    }
    return false;
  }
  Console::printf("API GET %s\n", url.c_str());
  const int code = http.GET();
  const String body = http.getString();
  http.end();
  if (code != HTTP_CODE_OK) {
    if (err != nullptr) {
      *err = String("http ") + code;
    }
    return false;
  }
  if (body.indexOf("\"ok\"") < 0) {
    if (err != nullptr) {
      *err = "bad health";
    }
    return false;
  }
  return true;
}

bool ApiClient::process(const ProcessRequest &req, ProcessResponse &out) {
  out = ProcessResponse{};
  if (req.jpeg == nullptr || req.jpegLength == 0) {
    out.error = "empty jpeg";
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    out.error = "wifi";
    return false;
  }

  const char *boundary = "----SomniumFormBoundary";
  String prefix;
  prefix.reserve(512);
  auto addField = [&](const char *name, const String &value) {
    prefix += "--";
    prefix += boundary;
    prefix += "\r\nContent-Disposition: form-data; name=\"";
    prefix += name;
    prefix += "\"\r\n\r\n";
    prefix += value;
    prefix += "\r\n";
  };
  addField("style", req.styleId != nullptr ? req.styleId : "cartoon");
  if (req.takenAt != nullptr && req.takenAt[0] != '\0') {
    addField("taken_at", String(req.takenAt));
  }
  if (!isnan(req.lat) && !isnan(req.lon)) {
    addField("lat", String(req.lat, 5));
    addField("lon", String(req.lon, 5));
  }
  prefix += "--";
  prefix += boundary;
  prefix += "\r\nContent-Disposition: form-data; name=\"image\"; "
            "filename=\"capture.jpg\"\r\nContent-Type: image/jpeg\r\n\r\n";

  const String suffix = String("\r\n--") + boundary + "--\r\n";
  const size_t total = prefix.length() + req.jpegLength + suffix.length();
  uint8_t *body = static_cast<uint8_t *>(ps_malloc(total));
  if (body == nullptr) {
    body = static_cast<uint8_t *>(malloc(total));
  }
  if (body == nullptr) {
    out.error = "oom body";
    return false;
  }
  memcpy(body, prefix.c_str(), prefix.length());
  memcpy(body + prefix.length(), req.jpeg, req.jpegLength);
  memcpy(body + prefix.length() + req.jpegLength, suffix.c_str(),
         suffix.length());

  const String url = baseUrl() + ServerConfig::kPath;
  HTTPClient http;
  http.setTimeout(65535);
  http.setConnectTimeout(15000);
  if (!http.begin(url)) {
    free(body);
    out.error = "begin";
    return false;
  }
  http.addHeader("Content-Type",
                 String("multipart/form-data; boundary=") + boundary);

  Console::printf("API POST %s (%u B)\n", url.c_str(),
                  static_cast<unsigned>(total));
  // POST blocks for the whole cook; pump beforehand so LCD isn't frozen blank.
  pump();
  const int code = http.POST(body, total);
  free(body);

  if (code != HTTP_CODE_OK) {
    out.error = String("http ") + code;
    const String errBody = http.getString();
    if (errBody.length() > 0) {
      out.error += " ";
      out.error += errBody.substring(0, 120);
    }
    http.end();
    return false;
  }

  out.prompt = http.header("X-Somnium-Prompt");
  out.context = http.header("X-Somnium-Context");

  if (!readBinaryBody(http, &out.jpeg, &out.length)) {
    out.error = "read body";
    http.end();
    freeResponse(out);
    return false;
  }
  http.end();

  if (out.jpeg == nullptr || out.jpeg[0] != 0xFF || out.jpeg[1] != 0xD8) {
    out.error = "bad jpeg resp";
    freeResponse(out);
    return false;
  }
  if (!jpegHasEoi(out.jpeg, out.length)) {
    out.error = "truncated jpeg";
    freeResponse(out);
    return false;
  }
  return true;
}

void ApiClient::freeResponse(ProcessResponse &out) {
  free(out.jpeg);
  out.jpeg = nullptr;
  out.length = 0;
}

}  // namespace Somnium
