#include "ApiClient.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <math.h>

#include "Console.h"
#include "ServerConfig.h"

namespace Somnium {

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

  String url = String("http://") + ServerConfig::kHost + ":" +
               String(ServerConfig::kPort) + ServerConfig::kPath;
  HTTPClient http;
  // HTTPClient timeout is uint16_t milliseconds.
  const uint16_t timeoutMs =
      ServerConfig::kTimeoutMs > 60000 ? 60000
                                       : static_cast<uint16_t>(ServerConfig::kTimeoutMs);
  http.setTimeout(timeoutMs);
  if (!http.begin(url)) {
    free(body);
    out.error = "begin";
    return false;
  }
  http.addHeader("Content-Type",
                 String("multipart/form-data; boundary=") + boundary);

  Console::printf("API POST %s (%u B)\n", url.c_str(),
                  static_cast<unsigned>(total));
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

  WiFiClient *stream = http.getStreamPtr();
  const int len = http.getSize();
  if (len > 0 && stream != nullptr) {
    out.length = static_cast<size_t>(len);
    out.jpeg = static_cast<uint8_t *>(ps_malloc(out.length));
    if (out.jpeg == nullptr) {
      out.jpeg = static_cast<uint8_t *>(malloc(out.length));
    }
    if (out.jpeg == nullptr) {
      out.error = "oom resp";
      http.end();
      return false;
    }
    size_t got = 0;
    const unsigned long deadline = millis() + ServerConfig::kTimeoutMs;
    while (got < out.length && millis() < deadline) {
      if (stream->available() == 0) {
        delay(1);
        continue;
      }
      got += stream->readBytes(out.jpeg + got, out.length - got);
    }
    out.length = got;
  } else {
    const String payload = http.getString();
    out.length = payload.length();
    out.jpeg = static_cast<uint8_t *>(ps_malloc(out.length));
    if (out.jpeg == nullptr) {
      out.jpeg = static_cast<uint8_t *>(malloc(out.length));
    }
    if (out.jpeg == nullptr) {
      out.error = "oom resp";
      http.end();
      return false;
    }
    memcpy(out.jpeg, payload.c_str(), out.length);
  }
  http.end();

  if (out.length < 4 || out.jpeg[0] != 0xFF || out.jpeg[1] != 0xD8) {
    out.error = "bad jpeg resp";
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
