#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

PORT="${1:-}"
if [[ -z "$PORT" ]]; then
  mapfile -t PORTS < <(compgen -G "/dev/serial/by-id/usb-1a86_*" || true)
  if [[ ${#PORTS[@]} -eq 1 ]]; then
    PORT="${PORTS[0]}"
  elif [[ -e /dev/ttyACM0 ]]; then
    PORT=/dev/ttyACM0
  else
    echo "Usage: $0 /dev/ttyACM0" >&2
    exit 1
  fi
fi

if [[ ! -f include/wifi_credentials.h ]]; then
  echo "Missing include/wifi_credentials.h — copy from wifi_credentials.example.h" >&2
  exit 1
fi
if [[ ! -f include/server_config.h ]]; then
  echo "Note: no include/server_config.h — using server_config.example.h (set laptop IP!)" >&2
fi

echo "Flashing Somnium → $PORT"
pio run -t upload --upload-port "$PORT"
echo "Done. Monitor: pio device monitor --port $PORT"
