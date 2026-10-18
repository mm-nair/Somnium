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

echo "Somnium server on http://127.0.0.1:8080"
echo "  Run ./scripts/usb_bridge.sh in another terminal (ESP cooks over USB)."
echo "  providers: xAI prompt + xAI image + Open-Meteo context"

exec python -m somnium_server.app
