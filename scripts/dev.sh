#!/usr/bin/env bash
set -euo pipefail
cat <<'EOF'
Somnium bring-up
================

0) nix develop

1) One-time config
   cp include/wifi_credentials.example.h include/wifi_credentials.h
   cp include/server_config.example.h include/server_config.h
   # wifi_credentials.h  → hotspot SSID/pass (2.4 GHz)
   # server_config.h      → SOMNIUM_USE_SERVER 1 + laptop hotspot IP
   # creative/creative.json → modes/prompts; set "real": true to hit a model

2) Wi-Fi + server test (two terminals)
   # A — laptop on the hotspot
   ./scripts/server.sh
   # note printed IP, put in include/server_config.h as SOMNIUM_SERVER_HOST, re-flash if changed
   curl -s http://127.0.0.1:8080/health

   # B — camera
   ./scripts/flash.sh
   # Join same hotspot. Boot UI → pick mode → shutter.
   # With "real": false (default), server returns the same photo (placeholder).

3) UI-only / offline
   #define SOMNIUM_USE_SERVER 0  in server_config.h, re-flash
   # Shutter still shows local capture path without HTTP.

4) UI preview on the ThinkPad (no LCD needed)
   ./scripts/ui_preview.sh
   # Sidebar: Boot, Modes, Process, Review, Storage…
   # Click a row, or 1–9 / [ ] / Tab. Modes/Storage: ←/→
   # Review auto-returns to Modes (~2.2s), like a camera glance.
   ./scripts/ui_preview.sh --screen storage
   ./scripts/ui_preview.sh --export /tmp/somnium-ui

5) Camera-free image cook (xAI + Open-Meteo mood data)
   # XAI_KEY in .env
   ./scripts/cook.sh photo.jpg --style noir --at 2026-10-17T19:30:00Z
   # writes server/outputs/*_0_orig.jpg and *_1_dream.jpg

Hardware USB tests
   python tests/monitor_inputs.py
   python tests/capture_photo.py -o shot.jpg
EOF
