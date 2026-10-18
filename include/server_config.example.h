#pragma once

// Optional compile-time server defaults.
// Copy to include/server_config.h (gitignored). Do not put a real gallery
// token in either file. Set the token in the setup portal or in the
// gitignored header on the machine that flashes the camera.

#define SOMNIUM_SERVER_HOST "somnium.local"
#define SOMNIUM_SERVER_PORT 8080
#define SOMNIUM_SERVER_PATH "/v1/process"
#define SOMNIUM_SERVER_TLS 0

// Empty disables the camera's direct gallery POST.
#define SOMNIUM_GALLERY_URL ""
// Bearer token for https://somnium-red.vercel.app/api/upload. Keep this empty
// in git. The portal stores a token in NVS on the camera only.
#define SOMNIUM_GALLERY_TOKEN ""
