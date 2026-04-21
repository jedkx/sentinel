#!/usr/bin/env bash
set -euo pipefail

SERVICE_NAME="sentinel"
DROPIN_DIR="/etc/systemd/system/${SERVICE_NAME}.service.d"
DROPIN_FILE="${DROPIN_DIR}/override.conf"
DEFAULT_LOG_FILE="/var/log/sentinel.log"

require_cmd() {
  local c="$1"
  command -v "$c" >/dev/null 2>&1 || {
    echo "[ERR] Missing command: ${c}" >&2
    exit 1
  }
}

get_env_value() {
  local key="$1"
  local env_line
  env_line="$(systemctl show "${SERVICE_NAME}" -p Environment --value 2>/dev/null || true)"

  for item in ${env_line}; do
    if [[ "${item}" == ${key}=* ]]; then
      echo "${item#*=}"
      return 0
    fi
  done
  return 1
}

escape_for_unit() {
  local value="$1"
  value="${value//\\/\\\\}"
  value="${value//\"/\\\"}"
  printf '%s' "$value"
}

config_show() {
  echo "[INFO] Active Environment for ${SERVICE_NAME}:"
  systemctl show "${SERVICE_NAME}" -p Environment --value | tr ' ' '\n' | sed '/^$/d'
}

config_set() {
  local key="$1"
  local value="$2"
  local escaped
  escaped="$(escape_for_unit "${value}")"

  sudo mkdir -p "${DROPIN_DIR}"
  sudo touch "${DROPIN_FILE}"

  if ! sudo grep -q '^\[Service\]$' "${DROPIN_FILE}"; then
    echo "[Service]" | sudo tee -a "${DROPIN_FILE}" >/dev/null
  fi

  if sudo grep -q "^Environment=\"${key}=" "${DROPIN_FILE}"; then
    sudo sed -i "s|^Environment=\"${key}=.*\"$|Environment=\"${key}=${escaped}\"|" "${DROPIN_FILE}"
  else
    echo "Environment=\"${key}=${escaped}\"" | sudo tee -a "${DROPIN_FILE}" >/dev/null
  fi

  echo "[OK] Set ${key}"
}

config_unset() {
  local key="$1"
  if [[ ! -f "${DROPIN_FILE}" ]]; then
    echo "[INFO] No override file yet"
    return 0
  fi
  sudo sed -i "/^Environment=\"${key}=.*\"$/d" "${DROPIN_FILE}"
  echo "[OK] Unset ${key}"
}

reload_restart() {
  sudo systemctl daemon-reload
  sudo systemctl restart "${SERVICE_NAME}"
  echo "[OK] Reloaded and restarted ${SERVICE_NAME}"
}

log_clear() {
  local log_file
  log_file="$(get_env_value SENTINEL_LOG_FILE || true)"
  if [[ -z "${log_file}" ]]; then
    log_file="${DEFAULT_LOG_FILE}"
  fi
  sudo touch "${log_file}"
  sudo truncate -s 0 "${log_file}"
  echo "[OK] Cleared ${log_file}"
}

logs_cmd() {
  local lines="200"
  local follow="0"

  if [[ $# -ge 1 ]]; then
    lines="$1"
  fi
  if [[ $# -ge 2 && "$2" == "--follow" ]]; then
    follow="1"
  fi

  if [[ "${follow}" == "1" ]]; then
    sudo journalctl -u "${SERVICE_NAME}" -n "${lines}" -f
  else
    sudo journalctl -u "${SERVICE_NAME}" -n "${lines}"
  fi
}

show_web_url() {
  local enabled bind port
  enabled="$(get_env_value SENTINEL_WEB_ENABLE || echo 0)"
  bind="$(get_env_value SENTINEL_WEB_BIND || echo 127.0.0.1)"
  port="$(get_env_value SENTINEL_WEB_PORT || echo 9090)"

  if [[ "${enabled}" != "1" ]]; then
    echo "[INFO] Web is disabled (SENTINEL_WEB_ENABLE=0)"
    return 0
  fi
  echo "[INFO] Web UI: http://${bind}:${port}/ui"
}

usage() {
  cat <<EOF
sentinelctl - Sentinel helper CLI

Usage:
  sentinelctl status
  sentinelctl start|stop|restart
  sentinelctl logs [lines] [--follow]
  sentinelctl config-show
  sentinelctl config-set KEY VALUE
  sentinelctl config-unset KEY
  sentinelctl apply
  sentinelctl log-clear
  sentinelctl rotate-now
  sentinelctl web-url

Examples:
  sentinelctl config-set SENTINEL_WEB_ENABLE 1
  sentinelctl config-set SENTINEL_WEB_TOKEN my-secret-token
  sentinelctl apply
  sentinelctl logs 300 --follow
EOF
}

main() {
  require_cmd systemctl
  require_cmd journalctl

  local cmd="${1:-help}"
  case "${cmd}" in
    status)
      sudo systemctl status "${SERVICE_NAME}" --no-pager
      ;;
    start)
      sudo systemctl start "${SERVICE_NAME}"
      ;;
    stop)
      sudo systemctl stop "${SERVICE_NAME}"
      ;;
    restart)
      sudo systemctl restart "${SERVICE_NAME}"
      ;;
    logs)
      shift || true
      logs_cmd "$@"
      ;;
    config-show)
      config_show
      ;;
    config-set)
      if [[ $# -lt 3 ]]; then
        echo "[ERR] Usage: sentinelctl config-set KEY VALUE" >&2
        exit 1
      fi
      config_set "$2" "$3"
      ;;
    config-unset)
      if [[ $# -lt 2 ]]; then
        echo "[ERR] Usage: sentinelctl config-unset KEY" >&2
        exit 1
      fi
      config_unset "$2"
      ;;
    apply)
      reload_restart
      ;;
    log-clear)
      log_clear
      ;;
    rotate-now)
      require_cmd logrotate
      sudo logrotate -f /etc/logrotate.d/sentinel
      echo "[OK] Forced logrotate for sentinel"
      ;;
    web-url)
      show_web_url
      ;;
    help|--help|-h)
      usage
      ;;
    *)
      echo "[ERR] Unknown command: ${cmd}" >&2
      usage
      exit 1
      ;;
  esac
}

main "$@"
