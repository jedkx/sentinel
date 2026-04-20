#!/usr/bin/env bash
set -euo pipefail

if [[ "$EUID" -eq 0 ]]; then
  echo "[ERR] Do not run as root. Use your pi user with sudo permissions."
  exit 1
fi

BINARY_SRC="build/sentinel"
BINARY_DST="/usr/local/bin/sentinel"
SERVICE_SRC="scripts/sentinel.service"
SERVICE_DST="/etc/systemd/system/sentinel.service"

# ---------- dependencies ----------
echo "[*] Installing dependencies..."
sudo apt-get update -qq
sudo apt-get install -y --no-install-recommends \
    cmake build-essential liblgpio-dev

# ---------- build ----------
echo "[*] Building..."
mkdir -p build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release > /dev/null
cmake --build build --parallel "$(nproc)"

# ---------- install binary ----------
echo "[*] Installing binary to ${BINARY_DST}..."
sudo install -m 755 "${BINARY_SRC}" "${BINARY_DST}"

# ---------- install service ----------
echo "[*] Installing systemd service..."
sudo install -m 644 "${SERVICE_SRC}" "${SERVICE_DST}"
sudo systemctl daemon-reload
sudo systemctl enable sentinel.service
sudo systemctl restart sentinel.service

echo "[OK] Done."
echo "[*] Service status: sudo systemctl status sentinel --no-pager"
echo "[*] Live logs:      sudo journalctl -u sentinel -f"