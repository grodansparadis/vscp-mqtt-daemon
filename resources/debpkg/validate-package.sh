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

validation_user="$(id -un)"

run_as_validation_user() {
  if [ "$(id -u)" -eq 0 ]; then
    if [ "$validation_user" = "root" ]; then
      "$@"
    elif command -v runuser >/dev/null 2>&1; then
      runuser -u "$validation_user" -- "$@"
    else
      sudo -u "$validation_user" "$@"
    fi
  else
    if [ "$validation_user" = "$(id -un)" ]; then
      "$@"
    else
      sudo -u "$validation_user" "$@"
    fi
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

work_dir="$(mktemp -d -p "$PWD" .validate-package.XXXXXX)"
broker_pid=""
cleanup() {
  if [ -n "${broker_pid:-}" ] && kill -0 "$broker_pid" 2>/dev/null; then
    kill -TERM "$broker_pid" 2>/dev/null || true
    wait "$broker_pid" 2>/dev/null || true
  fi
  rm -rf "$work_dir"
}
trap cleanup EXIT

create_validation_config() {
  local config_path="$work_dir/mqttvscpd-validation.json"
  python3 - "$config_path" "$validation_user" <<'PY'
import json
import sys

source_path = "/etc/vscp/mqttvscpd.json"
target_path = sys.argv[1]
validation_user = sys.argv[2]

with open(source_path, "r", encoding="utf-8") as input_file:
    config = json.load(input_file)


def replace_hosts(value):
    if isinstance(value, dict):
        return {key: replace_hosts(item) for key, item in value.items()}
    if isinstance(value, list):
        return [replace_hosts(item) for item in value]
    if value == "test.mosquitto.org":
        return "127.0.0.1"
    return value

updated = replace_hosts(config)
updated["runasuser"] = validation_user

with open(target_path, "w", encoding="utf-8") as output_file:
    json.dump(updated, output_file, indent=2)
    output_file.write("\n")
PY
  chmod 0644 "$config_path"
  printf '%s\n' "$config_path"
}

create_local_broker_config() {
  local broker_config_path="$work_dir/mosquitto-validation.conf"
  cat >"$broker_config_path" <<'EOF'
listener 1883 127.0.0.1
allow_anonymous true
persistence false
log_type error
log_dest stdout
EOF
  printf '%s\n' "$broker_config_path"
}

local_broker_running() {
  python3 - <<'PY'
import socket
import sys

sock = socket.socket()
sock.settimeout(0.5)
try:
    sock.connect(("127.0.0.1", 1883))
except OSError:
    sys.exit(1)
else:
    sock.close()
    sys.exit(0)
PY
}

start_local_broker() {
  local broker_config_path
  broker_config_path="$(create_local_broker_config)"

  if local_broker_running; then
    return 0
  fi

  if ! command -v mosquitto >/dev/null 2>&1; then
    echo "mosquitto is required for package validation" >&2
    exit 1
  fi

  mosquitto -c "$broker_config_path" >"$work_dir/mosquitto-validation.log" 2>&1 &
  broker_pid=$!

  sleep 2
  if ! kill -0 "$broker_pid" 2>/dev/null; then
    echo "Failed to start local MQTT broker" >&2
    cat "$work_dir/mosquitto-validation.log" >&2
    exit 1
  fi
}

start_and_stop_daemon() {
  local log_file config_path
  log_file="$work_dir/mqttvscpd-validation.log"
  config_path="$(create_validation_config)"
  start_local_broker

  run_as_validation_user /usr/sbin/mqttvscpd -s -c "$config_path" >"$log_file" 2>&1 &
  local pid=$!

  sleep 8
  if ! kill -0 "$pid" 2>/dev/null; then
    echo "Daemon failed to start" >&2
    cat "$log_file" >&2
    exit 1
  fi

  kill -TERM "$pid"
  wait "$pid" || true
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
