#pragma once

#include <Arduino.h>

#include "Camera.h"

namespace Somnium {

struct ProcessRequest {
  const uint8_t *jpeg = nullptr;
  size_t jpegLength = 0;
  const char *styleId = "cartoon";
  const char *takenAt = nullptr;  // ISO UTC or null
  float lat = NAN;
  float lon = NAN;
};

struct ProcessResponse {
  uint8_t *jpeg = nullptr;
  size_t length = 0;
  String prompt;
  String context;
  String error;
};

class ApiClient {
 public:
  using PumpFn = void (*)();

  // Optional UI/keepalive pump called while waiting on the network.
  static void setPump(PumpFn fn);

  // GET /health — true if laptop brain is reachable.
  static bool health(String *err = nullptr);

  // POST multipart → image/jpeg body into response.jpeg (PSRAM/heap).
  // Caller frees with freeResponse().
  static bool process(const ProcessRequest &req, ProcessResponse &out);
  static void freeResponse(ProcessResponse &out);
};

}  // namespace Somnium
