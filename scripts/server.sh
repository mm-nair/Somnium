#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT/server"

# Creative: ../creative/creative.json
# Plumbing: ./settings.json   Secrets: ../.env (XAI_KEY)

if [[ -f "$ROOT/.env" ]]; then
  set -a
  # shellcheck disable=SC1091
  source "$ROOT/.env"
  set +a
  export XAI_API_KEY="${XAI_API_KEY:-${XAI_KEY:-}}"
  export XAI_KEY="${XAI_KEY:-${XAI_API_KEY:-}}"
fi

export PYTHONPATH="$ROOT/server${PYTHONPATH:+:$PYTHONPATH}"

PORT="${SOMNIUM_PORT:-8080}"
echo "Somnium server on http://127.0.0.1:${PORT}"
if [[ -n "${SOMNIUM_PROCESS_TOKEN:-}" ]]; then
  echo "  POST /v1/process requires X-Somnium-Token"
else
  echo "  POST /v1/process is open (set SOMNIUM_PROCESS_TOKEN before exposing it)"
fi
echo "  Wi-Fi cook: ./scripts/wifi_bridge.sh    USB cook: ./scripts/usb_bridge.sh"
echo "  Remote host: server/README.md"
echo "  providers: xAI prompt + xAI image + Open-Meteo context"

exec python -m somnium_server.app
