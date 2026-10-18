#!/usr/bin/env bash
# Camera-free check that the laptop server is reachable the way the ESP will use it.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PORT="${SOMNIUM_SERVER_PORT:-8080}"

echo "=== Somnium Wi-Fi server check (no camera required) ==="
echo
echo "1) ./scripts/server.sh is running on this machine."
echo "2) The camera and this machine share a 2.4 GHz LAN."
echo "   ESP32-S3 cannot join 5 GHz. iPhone hotspots often isolate clients."
echo "3) Portal: join SoftAP Somnium-Setup (hold shutter + dial 1.5s)."
echo

IP="$(ip -4 route get 1.1.1.1 2>/dev/null | awk '{for(i=1;i<=NF;i++) if($i=="src"){print $(i+1); exit}}' || true)"
echo "This machine's primary IP: ${IP:-unknown}"
hostname -I 2>/dev/null | awk '{print "All IPs:", $0}' || true
echo

echo "Hitting http://127.0.0.1:${PORT}/health …"
if ! curl -sf "http://127.0.0.1:${PORT}/health" | tee /tmp/somnium-health.json; then
  echo
  echo "FAIL — start ./scripts/server.sh first."
  exit 1
fi
echo
python3 - <<'PY'
import json
payload = json.load(open("/tmp/somnium-health.json"))
print("mdns:", payload.get("mdns"))
print("upload:", payload.get("upload"))
if payload.get("mdns") != "somnium.local" or payload.get("upload") != "/v1/process":
    raise SystemExit("health is missing the Wi-Fi fields")
PY

if [[ -n "${IP:-}" ]]; then
  echo
  echo "Camera portal host can be: ${IP}"
  echo "  or somnium.local when mDNS is up (SOMNIUM_MDNS=0 disables it)."
  echo "Cook URL: http://${IP}:${PORT}/v1/process"
fi

echo
echo "Firmware and contract tests, still with no board:"
echo "  nix develop -c pio run"
echo "  nix develop -c python -m unittest discover -s tests -p 'test_*.py' -v"
echo
echo "On a flashed camera, serial should show LINK WiFi* and HTTP POST."
echo "Pull the LAN (leave USB) and the next shot should log usb fallback."
