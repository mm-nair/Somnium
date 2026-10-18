#!/usr/bin/env bash
# Camera-free cook: test photo + style + timestamp through xAI pipeline.
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
