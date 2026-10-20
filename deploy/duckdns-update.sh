#!/bin/sh
# Point a free DuckDNS name at this machine's current public address.
# https://www.duckdns.org/ — create a subdomain, then set DUCKDNS_DOMAIN
# (the name only, not the .duckdns.org suffix) and DUCKDNS_TOKEN in deploy/.env.
# The token is written to a mode 600 curl config. It is not placed on the
# curl command line, so a failure does not print it.
set -eu
cd "$(dirname "$0")"
set -a
# shellcheck disable=SC1091
. ./.env
set +a
: "${DUCKDNS_DOMAIN:?set DUCKDNS_DOMAIN in deploy/.env}"
: "${DUCKDNS_TOKEN:?set DUCKDNS_TOKEN in deploy/.env}"
cfg=$(mktemp)
chmod 600 "$cfg"
trap 'rm -f "$cfg"' EXIT
printf 'url = "https://www.duckdns.org/update?domains=%s&token=%s&ip="\n' \
  "$DUCKDNS_DOMAIN" "$DUCKDNS_TOKEN" > "$cfg"
if ! out=$(curl --silent --fail --config "$cfg"); then
  echo "duckdns update failed" >&2
  exit 1
fi
printf '%s\n' "$out"
[ "$out" = "OK" ]
