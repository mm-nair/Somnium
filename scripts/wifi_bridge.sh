#!/usr/bin/env bash
# Connect to the camera over Wi-Fi and cook through the local server.
#   ./scripts/wifi_bridge.sh
#   ./scripts/wifi_bridge.sh --host 192.168.1.20
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
exec python scripts/wifi_bridge.py "$@"
