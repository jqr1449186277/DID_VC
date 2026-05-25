#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="${PROJECT_ROOT:-$(cd "$SCRIPT_DIR/../.." && pwd)}"
ENV_FILE="${ENV_FILE:-$PROJECT_ROOT/run/zk_chain_comm_env.sh}"
MODE="${MODE:-ALL}"
OUT_DIR="${OUT_DIR:-$PROJECT_ROOT/results/$(date +%Y%m%d_%H%M%S)_M_chain_comm_suite}"

if [ -f "$ENV_FILE" ]; then
  # shellcheck disable=SC1090
  source "$ENV_FILE"
else
  RPC_URL="${RPC_URL:-http://127.0.0.1:8545}"
  BB_ORIGIN_URL="${BB_ORIGIN_URL:-http://127.0.0.1:3000}"
  PIRATE_ORIGIN_URL="${PIRATE_ORIGIN_URL:-http://127.0.0.1:4000}"
  VERIFY_ORIGIN_URL="${VERIFY_ORIGIN_URL:-http://127.0.0.1:3400}"
  COMMITTEE_ORIGIN_URLS="${COMMITTEE_ORIGIN_URLS:-http://127.0.0.1:8001,http://127.0.0.1:8002,http://127.0.0.1:8003,http://127.0.0.1:8004,http://127.0.0.1:8005,http://127.0.0.1:8006}"
  HTTP_PROXY_BB_PORT="${HTTP_PROXY_BB_PORT:-3100}"
  HTTP_PROXY_PIRATE_PORT="${HTTP_PROXY_PIRATE_PORT:-3101}"
  HTTP_PROXY_VERIFY_PORT="${HTTP_PROXY_VERIFY_PORT:-3401}"
  HTTP_PROXY_COMMITTEE_BASE_PORT="${HTTP_PROXY_COMMITTEE_BASE_PORT:-3201}"
fi
# shellcheck disable=SC1090
source "$PROJECT_ROOT/experiments/lib/common_chain_comm.sh"

trap 'chain_comm_stop_all' EXIT

chain_comm_mkdirs
chain_comm_write_meta
chain_comm_collect_process_snapshot

chain_comm_require_cmd node
chain_comm_require_cmd python3
chain_comm_require_cmd curl
chain_comm_require_cmd bash

chain_comm_log "PROJECT_ROOT=$PROJECT_ROOT"
chain_comm_log "OUT_DIR=$OUT_DIR"
chain_comm_log "MODE=$MODE"
chain_comm_log "RPC_URL=${RPC_URL:-}"
chain_comm_log "BB_ORIGIN_URL=${BB_ORIGIN_URL:-}"
chain_comm_log "PIRATE_ORIGIN_URL=${PIRATE_ORIGIN_URL:-}"
chain_comm_log "VERIFY_ORIGIN_URL=${VERIFY_ORIGIN_URL:-}"
chain_comm_log "COMMITTEE_ORIGIN_URLS=${COMMITTEE_ORIGIN_URLS:-}"

chain_comm_start_gas_monitor
chain_comm_start_proxy "bb" "$HTTP_PROXY_BB_PORT" "$BB_ORIGIN_URL"

if [ -n "${PIRATE_ORIGIN_URL:-}" ] && [ -n "${HTTP_PROXY_PIRATE_PORT:-}" ]; then
  chain_comm_start_proxy "pirate" "$HTTP_PROXY_PIRATE_PORT" "$PIRATE_ORIGIN_URL"
fi

if [ -n "${VERIFY_ORIGIN_URL:-}" ] && [ -n "${HTTP_PROXY_VERIFY_PORT:-}" ]; then
  chain_comm_start_proxy "verify" "$HTTP_PROXY_VERIFY_PORT" "$VERIFY_ORIGIN_URL"
fi

IFS=',' read -r -a committee_arr <<< "${COMMITTEE_ORIGIN_URLS:-}"
idx=0
for u in "${committee_arr[@]}"; do
  port=$((HTTP_PROXY_COMMITTEE_BASE_PORT + idx))
  chain_comm_start_proxy "committee$((idx+1))" "$port" "$u"
  idx=$((idx + 1))
done

chain_comm_wait_http_ready "http://127.0.0.1:${HTTP_PROXY_BB_PORT}/health" "proxy_bb"

if [ -n "${HTTP_PROXY_PIRATE_PORT:-}" ]; then
  chain_comm_wait_http_ready "http://127.0.0.1:${HTTP_PROXY_PIRATE_PORT}/health" "proxy_pirate"
fi

if [ -n "${HTTP_PROXY_VERIFY_PORT:-}" ]; then
  chain_comm_wait_http_ready "http://127.0.0.1:${HTTP_PROXY_VERIFY_PORT}/health" "proxy_verify"
fi

idx=0
for _u in "${committee_arr[@]}"; do
  port=$((HTTP_PROXY_COMMITTEE_BASE_PORT + idx))
  chain_comm_wait_http_ready "http://127.0.0.1:${port}/health" "proxy_committee$((idx+1))"
  idx=$((idx + 1))
done

chain_comm_build_proxy_urls
chain_comm_log "CHAIN_COMM_BB_URL=$CHAIN_COMM_BB_URL"
chain_comm_log "CHAIN_COMM_PIRATE_URL=${CHAIN_COMM_PIRATE_URL:-}"
chain_comm_log "CHAIN_COMM_VERIFY_URL=${CHAIN_COMM_VERIFY_URL:-}"
chain_comm_log "CHAIN_COMM_COMMITTEE_URLS=${CHAIN_COMM_COMMITTEE_URLS:-}"

export BASE_URL="$CHAIN_COMM_BB_URL"
export BB="$CHAIN_COMM_BB_URL"
export COMMITTEE_URLS="$CHAIN_COMM_COMMITTEE_URLS"
export PIRATE="${CHAIN_COMM_PIRATE_URL:-}"
export PIRATE_URL="${CHAIN_COMM_PIRATE_URL:-}"
export VERIFY_URL="${CHAIN_COMM_VERIFY_URL:-}"
if [ -n "${CHAIN_COMM_VERIFY_URL:-}" ]; then
  export VERIFY_HEALTH="$CHAIN_COMM_VERIFY_URL/health"
  export DIDZK_VERIFY_SERVICE_HEALTH="$CHAIN_COMM_VERIFY_URL/health"
fi

run_G() {
  mkdir -p "$OUT_DIR/G"
  chain_comm_log "running MODE=G via experiments/runs/run_G_zk_auth.sh"
  OUT="$OUT_DIR/G" bash "$PROJECT_ROOT/experiments/runs/run_G_zk_auth.sh"
}

run_H() {
  mkdir -p "$OUT_DIR/H"
  chain_comm_log "running MODE=H via experiments/runs/run_H_zk_recovery.sh"
  OUT="$OUT_DIR/H" bash "$PROJECT_ROOT/experiments/runs/run_H_zk_recovery.sh"
}

run_J() {
  mkdir -p "$OUT_DIR/J"
  chain_comm_log "running MODE=J via experiments/runs/run_J_ttss_recover_batch.sh"
  OUT_DIR="$OUT_DIR/J" bash "$PROJECT_ROOT/experiments/runs/run_J_ttss_recover_batch.sh"
}

run_K() {
  mkdir -p "$OUT_DIR/K"
  chain_comm_log "running MODE=K via experiments/runs/run_K_ttss_trace_cases.sh"
  OUT_DIR="$OUT_DIR/K" bash "$PROJECT_ROOT/experiments/runs/run_K_ttss_trace_cases.sh"
}

run_L() {
  mkdir -p "$OUT_DIR/L"
  chain_comm_log "running MODE=L via experiments/runs/run_L_ttss_scale.sh"
  OUT_DIR="$OUT_DIR/L" bash "$PROJECT_ROOT/experiments/runs/run_L_ttss_scale.sh"
}

m="$MODE"
case "$m" in
  G) run_G ;;
  H) run_H ;;
  J) run_J ;;
  K) run_K ;;
  L) run_L ;;
  ALL)
    run_G
    run_H
    run_J
    run_K
    ;;
  *) chain_comm_fail "unknown MODE=$m" ;;
esac

python3 "$PROJECT_ROOT/experiments/tools/summarize_chain_comm.py" \
  --gas "$OUT_DIR/data/gas_events.jsonl" \
  --http "$OUT_DIR/data/http_events.jsonl" \
  --out-dir "$OUT_DIR"

chain_comm_log "done"
chain_comm_log "outputs: $OUT_DIR"
