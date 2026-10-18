#pragma once

// Copy to server_config.h (gitignored).
// Set SOMNIUM_USE_SERVER to 0 to revert to local-only capture → LCD.

#define SOMNIUM_USE_SERVER 1

// Laptop IP on the phone hotspot (from ./scripts/server.sh or `hostname -I`).
#define SOMNIUM_SERVER_HOST "172.20.10.2"
#define SOMNIUM_SERVER_PORT 8080
#define SOMNIUM_SERVER_PATH "/v1/process"

// Full AI cook can take a while; ApiClient read loop uses this (ms).
#define SOMNIUM_SERVER_TIMEOUT_MS 180000

// Venue for Open-Meteo mood (sent as lat/lon with each shot).
#define SOMNIUM_VENUE_LAT 47.6553f
#define SOMNIUM_VENUE_LON -122.3035f
