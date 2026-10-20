#!/bin/sh
# Root only long enough to make the persistent outputs directory writable,
# then drop to the somnium user. Fly mounts the volume as root.
set -eu
out="${SOMNIUM_OUTPUT_DIR:-/data/outputs}"
parent=$(dirname "$out")
case "$parent" in
  /|/app|/usr|/bin|/lib)
    echo "SOMNIUM_OUTPUT_DIR must be a subdirectory, not $out" >&2
    exit 1
    ;;
esac
mkdir -p "$out"
chown -R somnium:somnium "$parent"
exec runuser -u somnium -- python -m somnium_server.app
