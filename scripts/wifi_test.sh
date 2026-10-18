#!/usr/bin/env bash
# Verify laptop server + hotspot path before flashing the camera.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PORT="${SOMNIUM_SERVER_PORT:-8080}"

echo "=== Somnium Wi‑Fi / server check ==="
echo
echo "1) Phone hotspot on (2.4 GHz). Laptop joined."
echo "2) In another terminal:  ./scripts/server.sh"
echo "3) Put the printed IP into include/server_config.h as SOMNIUM_SERVER_HOST"
echo

IP="$(ip -4 route get 1.1.1.1 2>/dev/null | awk '{for(i=1;i<=NF;i++) if($i=="src"){print $(i+1); exit}}' || true)"
echo "This machine's primary IP: ${IP:-unknown}"
hostname -I 2>/dev/null | awk '{print "All IPs:", $0}' || true
echo

echo "Hitting http://127.0.0.1:${PORT}/health …"
if curl -sf "http://127.0.0.1:${PORT}/health" | tee /tmp/somnium-health.json; then
  echo
  echo "OK — server is up locally."
else
  echo
  echo "FAIL — start ./scripts/server.sh first."
  exit 1
fi

if [[ -n "${IP:-}" ]]; then
  echo
  echo "From hotspot peers the camera will use: http://${IP}:${PORT}"
  echo "include/server_config.h should contain:"
  echo "  #define SOMNIUM_SERVER_HOST \"${IP}\""
fi

echo
echo "Camera serial (after flash) should show:"
echo "  WIFI connecting …"
echo "  WIFI <ip> <rssi>"
echo "  API <host> style=… t=2026-…"
echo "  API ok <bytes>  <mood summary>"
echo
echo "Then: Modes → shutter → Process → Review (dream) → dial Storage →"
echo "spin dial to see STYLE·raw / STYLE·mod alternating."
