#!/usr/bin/env bash

: "${DEV_LOG_PREFIX:?DEV_LOG_PREFIX must be set by scripts/dev_config.sh}"

log() { echo "[$DEV_LOG_PREFIX] $*"; }
die() { echo "[$DEV_LOG_PREFIX] FATAL: $*" >&2; exit 1; }
need_cmd() { command -v "$1" >/dev/null 2>&1 || die "missing command: $1"; }

dev_require_base_commands() {
  need_cmd bash
  need_cmd curl
  need_cmd jq
  need_cmd node
  need_cmd npx
  need_cmd python3
  need_cmd pgrep
}

assert_paths() {
  [[ -d "$PROJECT_ROOT" ]] || die "PROJECT_ROOT not found: $PROJECT_ROOT"
  [[ -d "$CPP_DIR" ]] || die "CPP_DIR not found: $CPP_DIR"
  [[ -d "$HARDHAT_DIR" ]] || die "HARDHAT_DIR not found: $HARDHAT_DIR"
  [[ -f "$PIRATE_JS" ]] || die "missing pirate_box.js: $PIRATE_JS"
  [[ -f "$TRACER_JS" ]] || die "missing tracer_client.js: $TRACER_JS"
  [[ -f "$BB_SERVICE_JS" ]] || die "missing bb_service_zk.js: $BB_SERVICE_JS"
  [[ -f "$VERIFIER_JS" ]] || die "missing zk_verify_service.js: $VERIFIER_JS"
  [[ -f "$HARDHAT_DIR/$DEPLOY_SCRIPT" ]] || die "missing deploy script: $HARDHAT_DIR/$DEPLOY_SCRIPT"
  [[ -d "$HARDHAT_DIR/node_modules" ]] || die "hardhat/node_modules missing; run: cd $HARDHAT_DIR && npm install"
  [[ -x "$MAIN_BIN" ]] || die "main binary missing or not executable: $MAIN_BIN"
  [[ -x "$COMMITTEE_BIN" ]] || die "committee binary missing or not executable: $COMMITTEE_BIN"
}

rpc_ready() {
  curl -fsS "$RPC_URL" -H 'Content-Type: application/json' \
    -d '{"jsonrpc":"2.0","method":"eth_chainId","params":[],"id":1}' >/dev/null 2>&1
}

wait_http() {
  local url="$1"
  local tries="${2:-$WAIT_SEC}"
  local i
  for i in $(seq 1 "$tries"); do
    if curl -fsS "$url" >/dev/null 2>&1; then
      return 0
    fi
    sleep 1
  done
  return 1
}

wait_http_or_pid() {
  local url="$1"
  local pid="$2"
  local name="$3"
  local tries="${4:-$WAIT_SEC}"
  local i
  for i in $(seq 1 "$tries"); do
    if curl -fsS "$url" >/dev/null 2>&1; then
      return 0
    fi
    if ! kill -0 "$pid" 2>/dev/null; then
      log "$name exited before $url became healthy"
      return 1
    fi
    sleep 1
  done
  return 1
}

wait_rpc() {
  local tries="${1:-$WAIT_SEC}"
  local i
  for i in $(seq 1 "$tries"); do
    if rpc_ready; then
      return 0
    fi
    sleep 1
  done
  return 1
}

stop_pid_file() {
  local pid_file="$1"
  local name="$2"
  [[ -f "$pid_file" ]] || return 0
  local pid
  pid="$(cat "$pid_file" 2>/dev/null || true)"
  if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
    kill "$pid" 2>/dev/null || true
    sleep 1
    if kill -0 "$pid" 2>/dev/null; then
      kill -9 "$pid" 2>/dev/null || true
    fi
    log "stopped $name pid=$pid"
  fi
  rm -f "$pid_file"
}

stop_by_pattern() {
  local pattern="$1"
  local name="$2"
  local pids
  pids="$(pgrep -f "$pattern" || true)"
  [[ -n "$pids" ]] || return 0
  echo "$pids" | xargs -r kill 2>/dev/null || true
  sleep 1
  echo "$pids" | xargs -r kill -9 2>/dev/null || true
  log "stopped $name via pattern"
}

committee_urls_csv() {
  local out=""
  local p
  for p in $COMMITTEE_PORTS; do
    if [[ -z "$out" ]]; then
      out="http://127.0.0.1:${p}"
    else
      out="$out,http://127.0.0.1:${p}"
    fi
  done
  printf '%s' "$out"
}

health_summary() {
  local ok=1
  if rpc_ready; then log "OK   rpc          $RPC_URL"; else log "BAD  rpc          $RPC_URL"; ok=0; fi
  if wait_http "$VERIFY_HEALTH" 1; then log "OK   verifier     $VERIFY_HEALTH"; else log "BAD  verifier     $VERIFY_HEALTH"; ok=0; fi
  if wait_http "$BASE_URL/health" 1; then log "OK   bb           $BASE_URL/health"; else log "BAD  bb           $BASE_URL/health"; ok=0; fi
  if wait_http "$PIRATE_URL/health" 1; then log "OK   pirate       $PIRATE_URL/health"; else log "BAD  pirate       $PIRATE_URL/health"; ok=0; fi
  local p
  for p in $COMMITTEE_PORTS; do
    if wait_http "http://127.0.0.1:${p}/health" 1; then
      log "OK   committee    http://127.0.0.1:${p}/health"
    else
      log "BAD  committee    http://127.0.0.1:${p}/health"
      ok=0
    fi
  done
  [[ -x "$MAIN_BIN" ]] || { log "BAD  main_bin     $MAIN_BIN"; ok=0; }
  [[ -x "$COMMITTEE_BIN" ]] || { log "BAD  committee_bin $COMMITTEE_BIN"; ok=0; }
  [[ "$ok" == "1" ]]
}

tail_logs() {
  echo
  log "recent log tails:"
  for f in \
    "$LOG_DIR/hardhat-node.log" \
    "$LOG_DIR/deploy_zk.log" \
    "$LOG_DIR/bb_service_zk.log" \
    "$LOG_DIR/zk_verify_service.log" \
    "$LOG_DIR/pirate_box.log"; do
    if [[ -f "$f" ]]; then
      echo "----- $f -----"
      tail -n 20 "$f" || true
    fi
  done
  local p
  for p in $COMMITTEE_PORTS; do
    local f="$LOG_DIR/committee_node_${p}.log"
    if [[ -f "$f" ]]; then
      echo "----- $f -----"
      tail -n 20 "$f" || true
    fi
  done
}
