#!/usr/bin/env bash
# One-pager: print the usual bring-up commands.
set -euo pipefail
cat <<'EOF'
Somnium bring-up (laptop hotspot + ESP32 edge client)
=====================================================

0) Enter shell
   nix develop

1) One-time config
   cp include/wifi_credentials.example.h include/wifi_credentials.h
   cp include/server_config.example.h include/server_config.h
   cp server/config.example.yaml server/config.yaml
   # edit wifi_credentials.h  → hotspot SSID/pass
   # edit server_config.h     → laptop IP on hotspot (see step 2)
   # edit server/config.yaml  → prompts + model providers

2) Run server (terminal A)
   ./scripts/server.sh
   # note printed IP → put in include/server_config.h as kServerHost
   curl -s http://127.0.0.1:8080/health | jq .

3) Flash camera (terminal B)
   ./scripts/flash.sh
   # or: ./scripts/flash.sh /dev/ttyACM0

4) Shoot
   Turn encoder to change style; press shutter.
   Echo image provider returns the same JPEG (pipeline test).
   Swap image.provider / prompt.provider in server/config.yaml when ready.

Revert to local-only (no Python server)
   In include/server_config.h set:  #define SOMNIUM_USE_SERVER 0
   Re-flash. Server/ folder can be ignored.

Hardware tests (no server)
   python tests/monitor_inputs.py
   python tests/capture_photo.py -o shot.jpg
EOF
