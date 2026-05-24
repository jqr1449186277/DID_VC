#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

ROOT="${ROOT:-${PROJECT_ROOT:-$(cd "$SCRIPT_DIR/../.." && pwd)}}"
RESULTS_DIR="${RESULTS_DIR:-$ROOT/results}"
BUILD_SCRIPT="${BUILD_SCRIPT:-$ROOT/scripts/build_ttss_binaries.sh}"
RUN_J_SCRIPT="${RUN_J_SCRIPT:-$ROOT/experiments/runs/run_J_ttss_recover_batch.sh}"
RUN_K_SCRIPT="${RUN_K_SCRIPT:-$ROOT/experiments/runs/run_K_ttss_trace_batch.sh}"
RUN_L_SCRIPT="${RUN_L_SCRIPT:-$ROOT/experiments/runs/run_L_ttss_scale.sh}"
SUMMARY_SCRIPT="${SUMMARY_SCRIPT:-$ROOT/experiments/tools/summarize_ttss_results.py}"
DO_BUILD="${DO_BUILD:-0}"
RUN_J="${RUN_J:-1}"
RUN_K="${RUN_K:-1}"
RUN_L="${RUN_L:-1}"
TS="$(date +%Y%m%d_%H%M%S)"
OUT_DIR="${OUT_DIR:-$RESULTS_DIR/${TS}_JKL_suite}"
mkdir -p "$OUT_DIR"
RUN_LOG="$OUT_DIR/run.log"
REPORT_DIR="$OUT_DIR/report"
log(){ echo "[run_JKL_suite] $*" | tee -a "$RUN_LOG"; }
die(){ log "FATAL: $*"; exit 1; }
[[ -f "$RUN_J_SCRIPT" ]] || die "missing $RUN_J_SCRIPT"
[[ -f "$RUN_K_SCRIPT" ]] || die "missing $RUN_K_SCRIPT"
[[ -f "$RUN_L_SCRIPT" ]] || die "missing $RUN_L_SCRIPT"
[[ -f "$SUMMARY_SCRIPT" ]] || die "missing $SUMMARY_SCRIPT"
if [[ "$DO_BUILD" == "1" ]]; then
  [[ -f "$BUILD_SCRIPT" ]] || die "missing $BUILD_SCRIPT"
  log "building binaries"
  bash "$BUILD_SCRIPT" all | tee -a "$RUN_LOG"
fi
J_DIR="$OUT_DIR/J_ttss_recover_batch"
K_DIR="$OUT_DIR/K_ttss_trace_batch"
L_DIR="$OUT_DIR/L_ttss_scale"
if [[ "$RUN_J" == "1" ]]; then
  log "running J recover batch"
  OUT_DIR="$J_DIR" bash "$RUN_J_SCRIPT" | tee -a "$RUN_LOG"
fi
if [[ "$RUN_K" == "1" ]]; then
  log "running K trace batch"
  OUT_DIR="$K_DIR" bash "$RUN_K_SCRIPT" | tee -a "$RUN_LOG"
fi
if [[ "$RUN_L" == "1" ]]; then
  log "running L scale"
  OUT_DIR="$L_DIR" bash "$RUN_L_SCRIPT" | tee -a "$RUN_LOG"
fi
log "summarizing results"
python3 "$SUMMARY_SCRIPT" --recover-dir "$J_DIR" --trace-dir "$K_DIR" --scale-dir "$L_DIR" --out-dir "$REPORT_DIR" | tee -a "$RUN_LOG"
log "done: suite_out=$OUT_DIR report=$REPORT_DIR"
