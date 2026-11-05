#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
CIA_FILE="${PROJECT_ROOT}/outputs/romm3ds.cia"

if [ $# -lt 1 ]; then
    echo "Usage: $0 <3ds-ip-address> [port]"
    echo "Example: $0 192.168.1.50"
    exit 1
fi

TARGET_IP="$1"
PORT="${2:-8000}"

if [ ! -f "${CIA_FILE}" ]; then
    echo "outputs/romm3ds.cia not found. Building CIA package first..."
    "${SCRIPT_DIR}/build-cia.sh"
fi

# Detect local IP address on Mac
LOCAL_IP=$(ipconfig getifaddr en0 2>/dev/null || ipconfig getifaddr en1 2>/dev/null || true)
if [ -z "${LOCAL_IP}" ]; then
    LOCAL_IP=$(python3 -c "import socket; s=socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.connect(('8.8.8.8', 80)); print(s.getsockname()[0]); s.close()" 2>/dev/null || echo "127.0.0.1")
fi

echo "=========================================="
echo "romm3ds Remote CIA Installer"
echo "=========================================="
echo "Host IP:    ${LOCAL_IP}"
echo "3DS IP:     ${TARGET_IP}"
echo "Local Port: ${PORT}"
echo "CIA URL:    http://${LOCAL_IP}:${PORT}/romm3ds.cia"
echo "=========================================="
echo ""
echo "Make sure FBI is open on your 3DS:"
echo "  Remote Install -> Receive URLs over the network"
echo ""

# Start lightweight HTTP server in background from outputs directory
python3 -m http.server "${PORT}" --directory "${PROJECT_ROOT}/outputs" >/dev/null 2>&1 &
HTTP_PID=$!

cleanup() {
    kill "${HTTP_PID}" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

# Send payload to FBI (Port 5000: 4-byte big-endian length + UTF-8 URL string)
python3 - <<EOF
import socket, struct, sys, time

target_ip = "${TARGET_IP}"
local_ip = "${LOCAL_IP}"
port = ${PORT}
url = f"http://{local_ip}:{port}/romm3ds.cia"

print(f"Connecting to FBI at {target_ip}:5000...")
try:
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(10.0)
    s.connect((target_ip, 5000))
    payload = url.encode("utf-8")
    s.sendall(struct.pack(">I", len(payload)) + payload)
    s.close()
    print("URL sent to FBI successfully!")
    print("FBI should now be downloading and installing romm3ds.cia...")
    print("Waiting for installation to finish (press Ctrl+C to abort server)...")
except Exception as e:
    print(f"Error connecting to FBI: {e}", file=sys.stderr)
    print("\nAlternative: In FBI, go to Remote Install -> Manually Enter URL:")
    print(f"  {url}\n")
    sys.exit(1)
EOF

# Keep HTTP server alive while FBI downloads
wait "${HTTP_PID}" 2>/dev/null || true

