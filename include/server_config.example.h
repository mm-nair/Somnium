#pragma once

// Copy to server_config.h (gitignored).
// Set SOMNIUM_USE_SERVER to 0 to revert to local-only capture → LCD.

#define SOMNIUM_USE_SERVER 1
#define SOMNIUM_SERVER_HOST "172.20.10.2"
#define SOMNIUM_SERVER_PORT 8080
#define SOMNIUM_SERVER_PATH "/v1/process"
#define SOMNIUM_SERVER_TIMEOUT_MS 60000
