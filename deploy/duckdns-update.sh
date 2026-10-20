#!/bin/sh
# Point a free DuckDNS name at this machine's current public address.
# https://www.duckdns.org/ — create a subdomain, then set DUCKDNS_DOMAIN
# (the name only, not the .duckdns.org suffix) and DUCKDNS_TOKEN in deploy/.env.
set -eu
cd "$(dirname "$0")"
set -a
# shellcheck disable=SC1091
. ./.env
set +a
: "${DUCKDNS_DOMAIN:?set DUCKDNS_DOMAIN in deploy/.env}"
: "${DUCKDNS_TOKEN:?set DUCKDNS_TOKEN in deploy/.env}"
out=$(
  curl -fsS \
    "https://www.duckdns.org/update?domains=${DUCKDNS_DOMAIN}&token=${DUCKDNS_TOKEN}&ip="
)
printf '%s\n' "$out"
[ "$out" = "OK" ]
