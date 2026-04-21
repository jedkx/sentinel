#!/usr/bin/env bash
set -euo pipefail

# Always operate from repository root (works even when called from scripts/).
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${REPO_ROOT}"

if [[ "${EUID}" -eq 0 ]]; then
  SUDO=""
else
  SUDO="sudo"
fi

BINARY_SRC="build/sentinel"
BINARY_DST="/usr/local/bin/sentinel"
CTL_SRC="scripts/sentinelctl.sh"
CTL_DST="/usr/local/bin/sentinelctl"
SERVICE_SRC="scripts/sentinel.service"
SERVICE_DST="/etc/systemd/system/sentinel.service"
LOGROTATE_SRC="scripts/sentinel.logrotate"
LOGROTATE_DST="/etc/logrotate.d/sentinel"
EPD_REQUIRED_FILE="vendor/e-Paper/RaspberryPi_JetsonNano/c/lib/Config/DEV_Config.c"
WAVESHARE_REPO_URL="${WAVESHARE_REPO_URL:-https://github.com/waveshareteam/e-Paper.git}"
WAVESHARE_REPO_REF="${WAVESHARE_REPO_REF:-master}"
SENTINEL_ENABLE_ON_BOOT="${SENTINEL_ENABLE_ON_BOOT:-1}"

require_file() {
  local path="$1"
  if [[ ! -f "${path}" ]]; then
    echo "[ERR] Missing required file: ${path}"
    exit 1
  fi
}

maybe_fetch_waveshare_vendor() {
  if [[ "${SENTINEL_DISABLE_DISPLAY:-0}" == "1" ]]; then
    echo "[*] SENTINEL_DISABLE_DISPLAY=1 -> skipping vendor fetch."
    return 0
  fi

  if [[ -f "${EPD_REQUIRED_FILE}" ]]; then
    echo "[*] Waveshare vendor sources found."
    return 0
  fi

  echo "[*] Waveshare vendor sources missing, attempting auto-fetch..."
  local tmp_dir
  tmp_dir="$(mktemp -d)"

  if git -c advice.detachedHead=false clone --depth 1 --filter=blob:none \
      --sparse --branch "${WAVESHARE_REPO_REF}" "${WAVESHARE_REPO_URL}" "${tmp_dir}" >/dev/null 2>&1; then
    git -C "${tmp_dir}" sparse-checkout set RaspberryPi_JetsonNano/c/lib >/dev/null 2>&1 || true
    mkdir -p "vendor/e-Paper/RaspberryPi_JetsonNano/c"
    rm -rf "vendor/e-Paper/RaspberryPi_JetsonNano/c/lib"
    cp -a "${tmp_dir}/RaspberryPi_JetsonNano/c/lib" "vendor/e-Paper/RaspberryPi_JetsonNano/c/"
    rm -rf "${tmp_dir}"
  else
    rm -rf "${tmp_dir}"
    echo "[WARN] Auto-fetch failed. Continuing with headless build."
    echo "[WARN] You can manually populate vendor/e-Paper from: ${WAVESHARE_REPO_URL}"
    return 0
  fi

  if [[ -f "${EPD_REQUIRED_FILE}" ]]; then
    echo "[*] Waveshare vendor sources fetched successfully."
  else
    echo "[WARN] Waveshare vendor layout mismatch after fetch. Continuing with headless build."
  fi
}

echo "[*] Validating repository files..."
require_file "${CTL_SRC}"
require_file "${SERVICE_SRC}"
require_file "${LOGROTATE_SRC}"

# ---------- dependencies ----------
echo "[*] Installing dependencies..."
${SUDO} apt-get update -qq
${SUDO} apt-get install -y --no-install-recommends \
  cmake build-essential liblgpio-dev logrotate git

# ---------- optional vendor fetch ----------
maybe_fetch_waveshare_vendor

# ---------- build ----------
echo "[*] Building..."
mkdir -p build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release > /dev/null
cmake --build build --parallel "$(nproc)"

# ---------- install binary ----------
echo "[*] Installing binary to ${BINARY_DST}..."
require_file "${BINARY_SRC}"
${SUDO} install -m 755 "${BINARY_SRC}" "${BINARY_DST}"

# ---------- install helper cli ----------
echo "[*] Installing helper CLI to ${CTL_DST}..."
${SUDO} install -m 755 "${CTL_SRC}" "${CTL_DST}"

# ---------- install service ----------
echo "[*] Installing systemd service..."
${SUDO} install -m 644 "${SERVICE_SRC}" "${SERVICE_DST}"

# ---------- install logrotate policy ----------
echo "[*] Installing logrotate policy..."
${SUDO} install -m 644 "${LOGROTATE_SRC}" "${LOGROTATE_DST}"

echo "[*] Reloading and restarting service..."
${SUDO} systemctl daemon-reload
if [[ "${SENTINEL_ENABLE_ON_BOOT}" == "1" ]]; then
  ${SUDO} systemctl enable sentinel.service
else
  ${SUDO} systemctl disable sentinel.service >/dev/null 2>&1 || true
  echo "[*] Auto-start on boot disabled (SENTINEL_ENABLE_ON_BOOT=${SENTINEL_ENABLE_ON_BOOT})."
fi
${SUDO} systemctl restart sentinel.service

echo "[*] Verifying deployment..."
${SUDO} systemctl is-active sentinel.service >/dev/null

echo "[OK] Sentinel deployed successfully."
echo "[*] Service status: ${SUDO} systemctl status sentinel --no-pager"
echo "[*] Live logs:      ${SUDO} journalctl -u sentinel -f"
echo "[*] Helper CLI:     ${CTL_DST}"