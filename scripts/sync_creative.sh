#!/usr/bin/env bash
# Regenerate Modes.h / CreativeGen.h / LogoBitmap.h from creative/
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
exec python tools/sync_creative.py "$@"
