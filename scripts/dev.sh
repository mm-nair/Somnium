#!/usr/bin/env bash
set -euo pipefail
cat <<'EOF'
Somnium bring-up
================

0) nix develop
   cp .env.example .env                 # XAI_KEY=...
   cp include/wifi_credentials.example.h include/wifi_credentials.h
   cp include/server_config.example.h include/server_config.h

1) Wi‑Fi + full AI suite
   # Phone hotspot (2.4 GHz). Laptop ON the hotspot.
   ./scripts/server.sh                  # terminal A — note the IP
   ./scripts/wifi_test.sh               # sanity-check /health
   # Edit include/server_config.h:
   #   SOMNIUM_USE_SERVER 1
   #   SOMNIUM_SERVER_HOST "<laptop hotspot IP>"
   ./scripts/flash.sh                   # terminal B
   # Serial should show: WIFI … then after shutter: API … API ok …
   # Flow: Modes → shutter → Process (mood+xAI) → Review (dream on LCD)
   #        dial-click → Storage → spin dial → STYLE·raw / STYLE·mod

2) Controls
   shutter     = always take a picture (Modes / Storage / Review)
   dial turn   = carousel (Modes) or browse plates (Storage)
   dial click  = Modes ↔ Storage  (also dismisses Review)

3) No LCD? / no camera?
   ./scripts/ui_preview.sh
   ./scripts/cook.sh photo.jpg --style anime --at 2026-10-17T19:30:00Z

4) Offline (no laptop brain)
   #define SOMNIUM_USE_SERVER 0  in server_config.h, re-flash

Creative edits → ./scripts/sync_creative.sh then re-flash
EOF
