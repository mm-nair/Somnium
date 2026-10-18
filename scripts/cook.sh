#!/usr/bin/env bash
# Camera-free cook: folder/file + style + timestamp → proc_output/{name}_dream.jpg
#   ./scripts/cook.sh process --style anime
#   ./scripts/cook.sh process/nyc.png --style anime
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

if [[ -f .env ]]; then
  set -a
  # shellcheck disable=SC1091
  source .env
  set +a
  export XAI_API_KEY="${XAI_API_KEY:-${XAI_KEY:-}}"
  export XAI_KEY="${XAI_KEY:-${XAI_API_KEY:-}}"
fi

# Default to in-process pipeline (no server required).
if [[ "$*" != *"--http"* ]] && [[ "$*" != *"--local"* ]]; then
  exec python tools/cook.py --local "$@"
fi
exec python tools/cook.py "$@"
