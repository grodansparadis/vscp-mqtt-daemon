#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 1 ]; then
  echo "Usage: $0 <package.deb>" >&2
  exit 2
fi

package="$1"
if [ ! -f "$package" ]; then
  echo "Package not found: $package" >&2
  exit 1
fi

if [ "$(id -u)" -ne 0 ]; then
  if command -v sudo >/dev/null 2>&1; then
    sudo_cmd=(sudo)
  else
    echo "This validation requires root privileges" >&2
    exit 1
  fi
else
  sudo_cmd=()
fi

run_cmd() {
  if [ "${#sudo_cmd[@]}" -gt 0 ]; then
    "${sudo_cmd[@]}" "$@"
  else
    "$@"
  fi
}

verify_exists() {
  local path="$1"
  if [ ! -e "$path" ]; then
    echo "Expected installed path is missing: $path" >&2
    exit 1
  fi
}

verify_removed() {
  local path="$1"
  if [ -e "$path" ]; then
    echo "Expected package artifact to be removed: $path" >&2
    exit 1
  fi
}

start_and_stop_daemon() {
  local log_file
  log_file="$(mktemp)"
  trap 'rm -f "$log_file"' RETURN

  /usr/sbin/mqttvscpd -s -c /etc/vscp/mqttvscpd.json >"$log_file" 2>&1 &
  local pid=$!

  sleep 8
  if ! kill -0 "$pid" 2>/dev/null; then
    echo "Daemon failed to start" >&2
    cat "$log_file" >&2
    exit 1
  fi

  kill -TERM "$pid"
  wait "$pid" || true
  rm -f "$log_file"
  trap - RETURN
}

run_cmd dpkg --purge mqttvscpd >/dev/null 2>&1 || true
run_cmd dpkg -i "$package"

verify_exists /usr/sbin/mqttvscpd
verify_exists /etc/vscp/mqttvscpd.json
verify_exists /etc/vscp/vscp.key
verify_exists /etc/logrotate.d/mqttvscpd
verify_exists /usr/lib/systemd/system/mqttvscpd.service
verify_exists /var/lib/vscp/mqttvscpd/vscp_events.sqlite3

start_and_stop_daemon

run_cmd dpkg --purge mqttvscpd

verify_removed /usr/sbin/mqttvscpd
verify_removed /etc/vscp/mqttvscpd.json
verify_removed /etc/vscp/vscp.key
verify_removed /etc/logrotate.d/mqttvscpd
verify_removed /usr/lib/systemd/system/mqttvscpd.service
verify_removed /var/lib/vscp/mqttvscpd
verify_removed /var/log/vscp
