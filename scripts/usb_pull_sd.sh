#!/usr/bin/env bash
# Pull ESP SD gallery (/somnium/*.jpg) over USB → ./demos/sd_<timestamp>/
# Stop usb_bridge first if it holds the port.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
if pgrep -f 'scripts/usb_bridge.py' >/dev/null 2>&1; then
  echo "usb_bridge is holding the serial port." >&2
  echo "  pkill -f usb_bridge.py" >&2
  echo "  $0 $*" >&2
  echo "  ./scripts/usb_bridge.sh" >&2
  exit 1
fi
exec python scripts/usb_pull_sd.py "$@"
