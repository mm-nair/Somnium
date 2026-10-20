#pragma once

// Copy to include/server_config.h (gitignored) and fill in what you use.
//
// Leave SOMNIUM_SERVER_HOST empty to keep the local cook: the board listens
// on somnium.local:8090, the laptop bridge posts to the laptop server, and
// no process token is sent.
//
// Direct cook to a public host (Fly terminates HTTPS on 443):
//   #define SOMNIUM_SERVER_HOST "your-app.fly.dev"
//   #define SOMNIUM_SERVER_PORT 443
//   #define SOMNIUM_SERVER_PATH "/v1/process"
//   #define SOMNIUM_SERVER_TLS 1
//   #define SOMNIUM_PROCESS_TOKEN "same value as SOMNIUM_PROCESS_TOKEN on the server"
//
// SOMNIUM_PROCESS_TOKEN is optional. When it is non-empty the cook POST
// carries X-Somnium-Token. Leave it empty for an open server.
// These are C string literals. Escape \ and ". Do not commit the real token.
//
// Gallery: set BOTH url and token only when the camera itself should upload
// the shot. The cook then sends skip_gallery=1 so the server does not upload
// the same shot. Leave both empty to let the server upload (its own
// SOMNIUM_UPLOAD_URL and SOMNIUM_UPLOAD_TOKEN). The gallery token is not the
// process token.

#define SOMNIUM_SERVER_HOST ""
#define SOMNIUM_SERVER_PORT 8080
#define SOMNIUM_SERVER_PATH "/v1/process"
#define SOMNIUM_SERVER_TLS 0
#define SOMNIUM_PROCESS_TOKEN ""
#define SOMNIUM_GALLERY_URL ""
#define SOMNIUM_GALLERY_TOKEN ""
