#!/usr/bin/env bash
set -euo pipefail
cat <<'EOF'
Somnium bring-up
================

0) nix develop

1) One-time config
   cp include/wifi_credentials.example.h include/wifi_credentials.h
   cp include/server_config.example.h include/server_config.h
   cp .env.example .env          # XAI_KEY=...
   # creative/creative.json      # modes, theme, logo, models
   # server/settings.json        # venue + API URLs
   ./scripts/sync_creative.sh    # after any creative/ edit

2) Server + camera (two terminals)
   ./scripts/server.sh           # note IP → SOMNIUM_SERVER_HOST, re-flash
   curl -s http://127.0.0.1:8080/health
   ./scripts/flash.sh

3) No LCD? Preview UI on the ThinkPad
   ./scripts/ui_preview.sh

4) No camera? Cook a test JPEG
   ./scripts/cook.sh photo.jpg --style noir --at 2026-10-17T19:30:00Z

5) Offline (no laptop brain)
   #define SOMNIUM_USE_SERVER 0  in server_config.h, re-flash

Hardware USB
   python tests/monitor_inputs.py
   python tests/capture_photo.py -o shot.jpg

Extend modes / colors / logo → creative/README.md
EOF
