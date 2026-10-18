#pragma once

#include <Arduino.h>

// Optional override: include/server_config.h (copy from server_config.example.h)
#if __has_include(<server_config.h>)
#include <server_config.h>
#endif

// Always cook on the laptop. Local raw-only "test mode" removed.
#ifndef SOMNIUM_USE_SERVER
#define SOMNIUM_USE_SERVER 1
#endif
#if !SOMNIUM_USE_SERVER
#error "SOMNIUM_USE_SERVER must be 1 — offline capture path removed"
#endif

#ifndef SOMNIUM_SERVER_HOST
#define SOMNIUM_SERVER_HOST "172.20.10.2"
#endif

#ifndef SOMNIUM_SERVER_PORT
#define SOMNIUM_SERVER_PORT 8080
#endif

#ifndef SOMNIUM_SERVER_PATH
#define SOMNIUM_SERVER_PATH "/v1/process"
#endif

#ifndef SOMNIUM_SERVER_TIMEOUT_MS
#define SOMNIUM_SERVER_TIMEOUT_MS 180000
#endif

#ifndef SOMNIUM_VENUE_LAT
#define SOMNIUM_VENUE_LAT 47.6553f
#endif

#ifndef SOMNIUM_VENUE_LON
#define SOMNIUM_VENUE_LON -122.3035f
#endif

namespace Somnium {
namespace ServerConfig {
constexpr const char *kHost = SOMNIUM_SERVER_HOST;
constexpr uint16_t kPort = SOMNIUM_SERVER_PORT;
constexpr const char *kPath = SOMNIUM_SERVER_PATH;
constexpr uint32_t kTimeoutMs = SOMNIUM_SERVER_TIMEOUT_MS;
constexpr float kVenueLat = SOMNIUM_VENUE_LAT;
constexpr float kVenueLon = SOMNIUM_VENUE_LON;
}  // namespace ServerConfig
}  // namespace Somnium
