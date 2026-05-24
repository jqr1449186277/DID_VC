#!/usr/bin/env bash
set -euo pipefail

chain_comm_now_ts() {
  date +%Y%m%d_%H%M%S
}

chain_comm_mkdirs() {
  mkdir -p "$OUT_DIR" "$OUT_DIR/logs" "$OUT_DIR/pids" "$OUT_DIR/data"
}

chain_comm_log() {
  local msg="$*"
  local ts
  ts="$(date '+%Y-%m-%dT%H:%M:%S')"
  mkdir -p "$OUT_DIR/logs"
  printf '[chain_comm][%s] %s\n' "$ts" "$msg" | tee -a "$OUT_DIR/logs/chain_comm.log" >&2
}

chain_comm_fail() {
  local msg="$*"
  chain_comm_log "FATAL: $msg"
  exit 1
}

chain_comm_die() {
  chain_comm_fail "$@"
}

chain_comm_require_cmd() {
  command -v "$1" >/dev/null 2>&1 || chain_comm_fail "missing command: $1"
}

chain_comm_tail_if_exists() {
  local f="$1"
  if [ -f "$f" ]; then
    tail -n 50 "$f" >&2 || true
  fi
}

chain_comm_write_meta() {
  chain_comm_mkdirs
  local meta_file="$OUT_DIR/run_meta.env"
  cat > "$meta_file" <<META
PROJECT_ROOT=${PROJECT_ROOT:-}
OUT_DIR=${OUT_DIR:-}
MODE=${MODE:-}
RPC_URL=${RPC_URL:-}
BB_ORIGIN_URL=${BB_ORIGIN_URL:-}
VERIFY_ORIGIN_URL=${VERIFY_ORIGIN_URL:-}
PIRATE_ORIGIN_URL=${PIRATE_ORIGIN_URL:-}
COMMITTEE_ORIGIN_URLS=${COMMITTEE_ORIGIN_URLS:-}
HTTP_PROXY_BB_PORT=${HTTP_PROXY_BB_PORT:-}
HTTP_PROXY_VERIFY_PORT=${HTTP_PROXY_VERIFY_PORT:-}
HTTP_PROXY_PIRATE_PORT=${HTTP_PROXY_PIRATE_PORT:-}
HTTP_PROXY_COMMITTEE_BASE_PORT=${HTTP_PROXY_COMMITTEE_BASE_PORT:-}
CHAIN_COMM_RUN_TAG=${CHAIN_COMM_RUN_TAG:-}
META
  chain_comm_log "wrote meta $meta_file"
}

chain_comm_collect_process_snapshot() {
  chain_comm_mkdirs
  local ps_file="$OUT_DIR/logs/process_snapshot.txt"
  {
    echo "# ts=$(date '+%Y-%m-%dT%H:%M:%S')"
    echo "# cwd=$(pwd)"
    echo "# whoami=$(whoami 2>/dev/null || true)"
    echo
    echo '## listening ports (selected)'
    ss -ltnp 2>/dev/null | grep -E ':(8545|3000|3100|320[0-9]|340[0-9]|4000)\b' || true
    echo
    echo '## related processes'
    ps -ef 2>/dev/null | grep -E 'gas_monitor.js|http_byte_proxy.py|bb_service_zk.js|node|python3' | grep -v grep || true
  } > "$ps_file"
  chain_comm_log "wrote process snapshot $ps_file"
}

chain_comm_start_gas_monitor() {
  local out_file="$OUT_DIR/data/gas_events.jsonl"
  local log_file="$OUT_DIR/logs/gas_monitor.log"
  local err_file="$OUT_DIR/logs/gas_monitor.err.log"
  local pid_file="$OUT_DIR/pids/gas_monitor.pid"

  env -u CONTRACT \
    RPC_URL="$RPC_URL" \
    PROJECT_ROOT="$PROJECT_ROOT" \
    OUT_FILE="$out_file" \
    AUTO_DETECT_WINDOW_BLOCKS="${AUTO_DETECT_WINDOW_BLOCKS:-20}" \
    AUTO_RETARGET="${AUTO_RETARGET:-1}" \
    STRICT_TARGET_MATCH="${STRICT_TARGET_MATCH:-0}" \
    BB_ORIGIN_URL="${BB_ORIGIN_URL:-}" \
    BASE_URL="${BASE_URL:-${BB_ORIGIN_URL:-}}" \
    node "$PROJECT_ROOT/experiments/lib/gas_monitor.js" \
      >"$log_file" 2>"$err_file" &

  echo $! > "$pid_file"
  sleep 0.3
  kill -0 "$(cat "$pid_file")" 2>/dev/null || {
    chain_comm_log "gas monitor exited early; see $err_file"
    chain_comm_tail_if_exists "$err_file"
    chain_comm_fail "gas monitor exited early"
  }
  chain_comm_log "gas monitor started pid=$(cat "$pid_file") out=$out_file"
}

chain_comm_start_proxy() {
  local target_name="$1"
  local listen_port="$2"
  local upstream="$3"
  local pid_file="$OUT_DIR/pids/proxy_${target_name}_${listen_port}.pid"
  local log_file="$OUT_DIR/logs/proxy_${target_name}_${listen_port}.log"
  local err_file="$OUT_DIR/logs/proxy_${target_name}_${listen_port}.err.log"
  local out_file="$OUT_DIR/data/http_events.jsonl"

  LISTEN_HOST="127.0.0.1" \
  LISTEN_PORT="$listen_port" \
  UPSTREAM="$upstream" \
  TARGET_NAME="$target_name" \
  OUT_FILE="$out_file" \
  python3 "$PROJECT_ROOT/experiments/lib/http_byte_proxy.py" \
    >"$log_file" 2>"$err_file" &

  echo $! > "$pid_file"
  sleep 0.3
  kill -0 "$(cat "$pid_file")" 2>/dev/null || {
    chain_comm_log "proxy $target_name exited early; see $err_file"
    chain_comm_tail_if_exists "$err_file"
    chain_comm_fail "proxy $target_name exited early"
  }
  chain_comm_log "proxy started target=$target_name port=$listen_port upstream=$upstream pid=$(cat "$pid_file")"
}

chain_comm_wait_http_ok() {
  local name="$1"
  local url="$2"
  local tries="${3:-40}"
  local delay="${4:-0.5}"
  local i
  for ((i=1; i<=tries; i++)); do
    if curl -fsS "$url" >/dev/null 2>&1; then
      chain_comm_log "health ok for $name at $url on try $i"
      return 0
    fi
    sleep "$delay"
  done
  chain_comm_fail "health check failed for $name at $url after $tries tries"
}

chain_comm_wait_http_ready() {
  # Compatibility with run_M_chain_comm_suite.sh calling convention:
  # chain_comm_wait_http_ready "URL" "NAME" [MAX_TRIES] [SLEEP_S]
  local url="${1:-}"
  local name="${2:-http}"
  local max_tries="${3:-40}"
  local sleep_s="${4:-0.5}"

  if [ -z "$url" ]; then
    chain_comm_fail "chain_comm_wait_http_ready: missing url"
  fi

  chain_comm_wait_http_ok "$name" "$url" "$max_tries" "$sleep_s"
}

chain_comm_stop_all() {
  if [ -d "$OUT_DIR/pids" ]; then
    find "$OUT_DIR/pids" -type f -name '*.pid' | while read -r f; do
      local pid
      pid="$(cat "$f" 2>/dev/null || true)"
      if [ -n "${pid:-}" ] && kill -0 "$pid" 2>/dev/null; then
        kill "$pid" 2>/dev/null || true
      fi
    done
  fi
}

chain_comm_build_proxy_urls() {
  CHAIN_COMM_BB_URL="http://127.0.0.1:${HTTP_PROXY_BB_PORT}"
  export CHAIN_COMM_BB_URL

  if [ -n "${HTTP_PROXY_VERIFY_PORT:-}" ]; then
    CHAIN_COMM_VERIFY_URL="http://127.0.0.1:${HTTP_PROXY_VERIFY_PORT}"
    export CHAIN_COMM_VERIFY_URL
  fi

  if [ -n "${HTTP_PROXY_PIRATE_PORT:-}" ]; then
    CHAIN_COMM_PIRATE_URL="http://127.0.0.1:${HTTP_PROXY_PIRATE_PORT}"
    export CHAIN_COMM_PIRATE_URL
  fi

  local proxy_urls=()
  IFS=',' read -r -a committee_arr <<< "${COMMITTEE_ORIGIN_URLS:-}"
  local idx=0
  for _u in "${committee_arr[@]}"; do
    local p=$((HTTP_PROXY_COMMITTEE_BASE_PORT + idx))
    proxy_urls+=("http://127.0.0.1:${p}")
    idx=$((idx + 1))
  done
  CHAIN_COMM_COMMITTEE_URLS="$(IFS=,; echo "${proxy_urls[*]}")"
  export CHAIN_COMM_COMMITTEE_URLS
}
