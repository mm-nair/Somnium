#!/bin/sh
# Root only long enough to make the persistent outputs directory writable,
# then drop to the somnium user. The Docker volume is mounted as root.
set -eu
if [ -z "${XAI_KEY:-}${XAI_API_KEY:-}" ]; then
  echo "XAI_KEY is required" >&2
  exit 1
fi
case "${SOMNIUM_REQUIRE_AUTH:-}" in
  1|true|TRUE|yes|YES|on|ON)
    if [ -z "${SOMNIUM_PROCESS_TOKEN:-}" ]; then
      echo "SOMNIUM_PROCESS_TOKEN is required when SOMNIUM_REQUIRE_AUTH=1" >&2
      exit 1
    fi
    ;;
esac
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
