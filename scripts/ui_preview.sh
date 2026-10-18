#!/usr/bin/env bash
# Desktop 320×240 Somnium UI preview (no LCD required).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
exec python tools/ui_preview.py "$@"
