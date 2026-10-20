#!/usr/bin/env bash
set -euo pipefail
cat <<'EOF'
Somnium bring-up (Wi-Fi cook, USB fallback)
========================================

0) nix develop
   cp .env.example .env                 # XAI_KEY=...
   cp include/wifi_secrets.example.h include/wifi_secrets.h
   cp include/server_config.example.h include/server_config.h
   # empty SOMNIUM_SERVER_HOST = somnium.local, no token
   # Fly: host, port 443, SOMNIUM_SERVER_TLS 1, SOMNIUM_PROCESS_TOKEN

1) ThinkPad brain + one bridge
   ./scripts/server.sh                  # terminal A — always while demoing
   ./scripts/wifi_bridge.sh             # terminal B — Wi-Fi cook (somnium.local:8090)
   ./scripts/usb_bridge.sh              # or this, when the Wi-Fi client is stopped

   Stop the USB bridge before anything else that needs the port:
     pkill -f usb_bridge.py
     ./scripts/flash.sh                 # Wi-Fi bridge reconnects on its own
     ./scripts/usb_pull_sd.sh           # → demos/sd_<timestamp>/

2) Shoot
   Modes → shutter → Process ("wifi > thinkpad...", "wifi > server...", or "usb > thinkpad...") → Review · dream (~20s+)
   dial-click → Storage → spin dial → STYLE·raw / STYLE·mod
   Bridge log should show: COOK … / DREAM … accepted by ESP / | HOST DREAM … / | UI review

3) Controls
   shutter     = always take a picture
   dial turn   = carousel (Modes) or browse plates (Storage)
   dial click  = Modes ↔ Storage  (also dismisses Review)

4) No LCD? / no camera?
   ./scripts/ui_preview.sh
   ./scripts/cook.sh photo.jpg --style anime

Creative edits → ./scripts/sync_creative.sh then re-flash
EOF
