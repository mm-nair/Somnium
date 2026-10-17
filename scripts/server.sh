#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT/server"

if [[ ! -f config.yaml ]]; then
  cp config.example.yaml config.yaml
  echo "Created server/config.yaml from example — edit prompts/models there."
fi

export PYTHONPATH="$ROOT/server${PYTHONPATH:+:$PYTHONPATH}"

echo "Listening on 0.0.0.0:8080 — put your hotspot IP into include/server_config.h"
echo -n "  primary: "
ip -4 route get 1.1.1.1 2>/dev/null | awk '{for(i=1;i<=NF;i++) if($i=="src"){print $(i+1); exit}}' || echo "?"
hostname -I 2>/dev/null | awk '{print "  all:", $0}' || true

exec python -m somnium_server.app
