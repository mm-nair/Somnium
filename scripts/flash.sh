#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

if pgrep -f 'scripts/usb_bridge.py' >/dev/null 2>&1; then
  echo "Stop usb_bridge before flashing: Ctrl-C in the bridge terminal." >&2
  exit 1
fi

PORT="${1:-}"
if [[ -z "$PORT" ]]; then
  mapfile -t PORTS < <(
    compgen -G "/dev/serial/by-id/usb-1a86_*" || true
    compgen -G "/dev/serial/by-id/usb-Espressif*" || true
  )
  if [[ ${#PORTS[@]} -ge 1 ]]; then
    PORT="$(readlink -f "${PORTS[0]}")"
  elif [[ -e /dev/ttyACM0 ]]; then
    PORT=/dev/ttyACM0
  else
    echo "Usage: $0 /dev/ttyACM0" >&2
    echo "Stop ./scripts/usb_bridge.sh first if it holds the port." >&2
    exit 1
  fi
fi

echo "Flashing Somnium → $PORT"
echo "Restart usb_bridge after flashing."
pio run -t upload --upload-port "$PORT"
echo "Done. Wi-Fi bridge reconnects on its own; otherwise: ./scripts/usb_bridge.sh"
