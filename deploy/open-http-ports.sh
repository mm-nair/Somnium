#!/bin/sh
# Oracle's Ubuntu image rejects packets the VCN security list already allowed.
# Docker publishes 80 and 443 through the FORWARD chain, not INPUT, and the
# image's FORWARD rule rejects with icmp-host-prohibited. Accept the ports on
# INPUT as well, for a process that is not behind Docker.
#
# Do not iptables-save while Docker is running. The DOCKER chains are created
# at daemon start and are not there when a boot-time restore runs, so a saved
# ruleset breaks the next boot. somnium.service runs this script again on boot.
set -eu
if ! command -v iptables >/dev/null 2>&1; then
  echo "iptables is not installed" >&2
  exit 1
fi

# Ubuntu can have the REJECT rule on iptables-legacy while `iptables` is
# nft. Docker's published ports are dropped if either backend still rejects
# FORWARD. Applying both is idempotent: -C skips a rule that is already there.
apply_firewall() {
  ipt=$1
  for port in 80 443; do
    if "$ipt" -C INPUT -p tcp --dport "$port" -j ACCEPT 2>/dev/null; then
      echo "$ipt: port $port already accepted on INPUT"
    else
      "$ipt" -I INPUT 1 -p tcp --dport "$port" -j ACCEPT
      echo "$ipt: accepted tcp $port on INPUT"
    fi
  done
  while "$ipt" -C FORWARD -j REJECT --reject-with icmp-host-prohibited 2>/dev/null; do
    "$ipt" -D FORWARD -j REJECT --reject-with icmp-host-prohibited
    echo "$ipt: removed FORWARD reject icmp-host-prohibited (Docker publishes via FORWARD)"
  done
}

apply_firewall iptables
if command -v iptables-legacy >/dev/null 2>&1; then
  apply_firewall iptables-legacy
fi
if command -v ufw >/dev/null 2>&1 && ufw status 2>/dev/null | grep -q "Status: active"; then
  ufw --force allow 80/tcp
  ufw --force allow 443/tcp
fi
