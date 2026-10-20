#!/usr/bin/env bash
set -euo pipefail
cat <<'EOF'
Somnium bring-up (USB cook)
===========================

0) nix develop
   cp .env.example .env                 # XAI_KEY=...

1) ThinkPad brain + USB bridge
   ./scripts/server.sh                  # terminal A — always while demoing
   ./scripts/usb_bridge.sh              # terminal B — ONLY while shooting
   # Or point the bridge at Fly (laptop still needed for USB):
   # SOMNIUM_SERVER_URL + SOMNIUM_PROCESS_TOKEN in .env
   # Deploy steps: server/README.md

   Stop bridge before anything else that needs the port:
     pkill -f usb_bridge.py
     ./scripts/flash.sh                 # then restart bridge
     ./scripts/usb_pull_sd.sh           # → demos/sd_<timestamp>/

2) Shoot
   Modes → shutter → Process ("usb → thinkpad…") → Review · dream (~20s+)
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
