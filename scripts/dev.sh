#!/usr/bin/env bash
set -euo pipefail
cat <<'EOF'
Somnium bring-up (USB cook)
===========================

0) nix develop
   cp .env.example .env                 # XAI_KEY=...

1) ThinkPad brain + USB bridge
   ./scripts/server.sh                  # terminal A
   ./scripts/usb_bridge.sh              # terminal B (holds /dev/ttyACM*)
   ./scripts/flash.sh                   # terminal C — stop bridge first, then restart it

2) Shoot
   Modes → shutter → Process ("usb → thinkpad…") → Review · dream (~20s+)
   dial-click → Storage → spin dial → STYLE·raw / STYLE·mod

2b) Save SD demos to ThinkPad (stop bridge first)
   pkill -f usb_bridge.py
   ./scripts/usb_pull_sd.sh            # → demos/sd_<timestamp>/
   ./scripts/usb_bridge.sh

3) Controls
   shutter     = always take a picture
   dial turn   = carousel (Modes) or browse plates (Storage)
   dial click  = Modes ↔ Storage  (also dismisses Review)

4) No LCD? / no camera?
   ./scripts/ui_preview.sh
   ./scripts/cook.sh photo.jpg --style anime

Creative edits → ./scripts/sync_creative.sh then re-flash
EOF
