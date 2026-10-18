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

echo "Listening on 0.0.0.0:8080 — put your hotspot IP into include/server_config.h"
echo "  providers: xAI prompt + xAI image + Open-Meteo context"
echo -n "  primary: "
ip -4 route get 1.1.1.1 2>/dev/null | awk '{for(i=1;i<=NF;i++) if($i=="src"){print $(i+1); exit}}' || echo "?"
hostname -I 2>/dev/null | awk '{print "  all:", $0}' || true

exec python -m somnium_server.app
