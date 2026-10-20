#!/bin/sh
# Oracle's Ubuntu image rejects packets that the VCN security list has already
# allowed. Open 80 and 443 on the instance firewall after the security list
# allows them. SSH (22) is left as the image shipped it.
set -eu
if ! command -v iptables >/dev/null 2>&1; then
  echo "iptables is not installed" >&2
  exit 1
fi
for port in 80 443; do
  if iptables -C INPUT -p tcp --dport "$port" -j ACCEPT 2>/dev/null; then
    echo "port $port already accepted"
    continue
  fi
  iptables -I INPUT 1 -p tcp --dport "$port" -j ACCEPT
  echo "accepted tcp $port"
done
if command -v netfilter-persistent >/dev/null 2>&1; then
  netfilter-persistent save
elif [ -d /etc/iptables ]; then
  iptables-save > /etc/iptables/rules.v4
else
  echo "rules are live but were not saved; install iptables-persistent" >&2
fi
