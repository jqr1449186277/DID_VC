#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# run_N_zk_depth_scale.sh
# Purpose:
#   Measure anonymous-auth cost under different Merkle tree depths.
#   Depth set default: 16,17,18,19,20
#   Metrics: path / witness / prove / verify
#   Modes: bb_each=0 and bb_each=1
#
# Design assumptions:
#   1) You already have a phase-5 style local environment:
#      hardhat + bb_service_zk + verifier + committee nodes.
#   2) start_stop.sh supports restart with TREE_DEPTH env.
#   3) The anonymous-auth batch script writes either:
#         <outdir>/summary.csv
#      or <outdir>/attempts.csv
#   4) The auth script can be driven by environment variables and/or flags below.
#
# Safe defaults:
#   - fixed recovery parameters remain n=6,t=4
#   - depths: 16..20
#   - runs per depth per mode: 50
#
# If your local auth script name differs, override:
#   AUTH_BATCH_SCRIPT=experiments/runs/run_G_zk_auth.sh bash experiments/runs/run_N_zk_depth_scale.sh

ROOT="${ROOT:-${PROJECT_ROOT:-$(cd "$SCRIPT_DIR/../.." && pwd)}}"
PROJECT_ROOT="$ROOT"
SCRIPTS_DIR="${SCRIPTS_DIR:-$ROOT/scripts}"
RESULTS_DIR="${RESULTS_DIR:-$ROOT/results}"
RUN_DIR="${RUN_DIR:-$ROOT/run}"

START_STOP_SCRIPT="${START_STOP_SCRIPT:-$SCRIPTS_DIR/start_stop.sh}"
ENV_FILE="${ENV_FILE:-$RUN_DIR/ttss_phase5_env.sh}"

# Optional depth-specific preparation hook:
# e.g. compile circuit / zkey / vk for each depth.
PREPARE_DEPTH_SCRIPT="${PREPARE_DEPTH_SCRIPT:-}"

# Prefer batch script, fallback to single auth runner if that is what exists.
if [[ -n "${AUTH_BATCH_SCRIPT:-}" ]]; then
  AUTH_BATCH_SCRIPT="$AUTH_BATCH_SCRIPT"
elif [[ -f "$ROOT/experiments/runs/run_G_zk_auth.sh" ]]; then
  AUTH_BATCH_SCRIPT="$ROOT/experiments/runs/run_G_zk_auth.sh"
else
  AUTH_BATCH_SCRIPT="$ROOT/experiments/runs/run_G_zk_auth.sh"
fi

DEPTH_LIST="${DEPTH_LIST:-16,17,18,19,20}"
BB_EACH_LIST="${BB_EACH_LIST:-0,1}"
RUNS_PER_POINT="${RUNS_PER_POINT:-50}"

# Fixed baseline, consistent with your current paper prototype.
TTSS_N="${TTSS_N:-6}"
TTSS_T="${TTSS_T:-4}"

# Generic HTTP / wait budgets; inherited by child scripts if they support them.
TIMEOUT_MS="${TIMEOUT_MS:-30000}"
REGISTER_WAIT_MS="${REGISTER_WAIT_MS:-120000}"
PATH_WAIT_MS="${PATH_WAIT_MS:-120000}"
ROOT_WAIT_MS="${ROOT_WAIT_MS:-60000}"
ROOT_POLL_MS="${ROOT_POLL_MS:-300}"

# Small cool down between depths.
DEPTH_SLEEP_SEC="${DEPTH_SLEEP_SEC:-3}"

# If 1, keep raw subdirectories from inner auth script.
KEEP_POINT_DIRS="${KEEP_POINT_DIRS:-1}"

# If 1, continue even when one point fails.
CONTINUE_ON_ERROR="${CONTINUE_ON_ERROR:-1}"

need_cmd() { command -v "$1" >/dev/null 2>&1 || { echo "[run_N_depth_scale] FATAL: missing command: $1" >&2; exit 1; }; }
log() { echo "[run_N_depth_scale] $*"; }
die() { echo "[run_N_depth_scale] FATAL: $*" >&2; exit 1; }

need_cmd bash
need_cmd awk
need_cmd sed
need_cmd python3
need_cmd curl

[[ -d "$ROOT" ]] || die "ROOT not found: $ROOT"
[[ -f "$START_STOP_SCRIPT" ]] || die "start/stop script not found: $START_STOP_SCRIPT"
[[ -f "$AUTH_BATCH_SCRIPT" ]] || die "auth batch script not found: $AUTH_BATCH_SCRIPT"

mkdir -p "$RESULTS_DIR"

TS="$(date +%Y%m%d_%H%M%S)"
OUTDIR="${OUTDIR:-$RESULTS_DIR/run_N_depth_scale_$TS}"
mkdir -p "$OUTDIR"

AGG_CSV="$OUTDIR/aggregate.csv"
RAW_INDEX_CSV="$OUTDIR/raw_index.csv"

cat > "$AGG_CSV" <<'CSV'
depth,bb_each,runs,pass_count,pass_rate,median_path_ms,median_witness_ms,median_prove_ms,median_verify_ms,avg_path_ms,avg_witness_ms,avg_prove_ms,avg_verify_ms,proof_bytes_median,public_bytes_median,point_dir,note
CSV

cat > "$RAW_INDEX_CSV" <<'CSV'
depth,bb_each,point_dir,inner_summary,inner_attempts
CSV

trim() {
  local x="$1"
  x="${x#"${x%%[![:space:]]*}"}"
  x="${x%"${x##*[![:space:]]}"}"
  printf '%s' "$x"
}

csv_to_lines() {
  local csv="$1"
  echo "$csv" | tr ',' '\n' | sed '/^[[:space:]]*$/d'
}

health_check() {
  local base="${BASE_URL:-http://127.0.0.1:3000}"
  local verify="${VERIFY_HEALTH:-http://127.0.0.1:3400/health}"
  local pirate="${PIRATE_URL:-http://127.0.0.1:4000}"
  log "health check: bb"
  curl -fsS "$base/health" >/dev/null
  log "health check: verifier"
  curl -fsS "$verify" >/dev/null
  log "health check: root"
  curl -fsS "$base/root" >/dev/null
  if curl -fsS "$pirate/health" >/dev/null 2>&1; then
    log "health check: pirate"
  fi
}

restart_for_depth() {
  local depth="$1"
  log "restart environment with TREE_DEPTH=$depth"
  env \
    PROJECT_ROOT="$PROJECT_ROOT" \
    TREE_DEPTH="$depth" \
    TTSS_N="$TTSS_N" \
    TTSS_T="$TTSS_T" \
    bash "$START_STOP_SCRIPT" restart >/dev/null

  if [[ -f "$ENV_FILE" ]]; then
    # shellcheck disable=SC1090
    source "$ENV_FILE"
  fi
  export PROJECT_ROOT ROOT RESULTS_DIR RUN_DIR SCRIPTS_DIR
  export TREE_DEPTH="$depth" TTSS_N TTSS_T
  export TIMEOUT_MS REGISTER_WAIT_MS PATH_WAIT_MS ROOT_WAIT_MS ROOT_POLL_MS
  health_check
}

prepare_depth() {
  local depth="$1"
  if [[ -n "$PREPARE_DEPTH_SCRIPT" ]]; then
    [[ -f "$PREPARE_DEPTH_SCRIPT" ]] || die "PREPARE_DEPTH_SCRIPT not found: $PREPARE_DEPTH_SCRIPT"
    log "prepare depth artifacts depth=$depth"
    env PROJECT_ROOT="$PROJECT_ROOT" TREE_DEPTH="$depth" bash "$PREPARE_DEPTH_SCRIPT"
  fi
}

find_summary_csv() {
  local dir="$1"
  find "$dir" -maxdepth 4 -type f -name 'summary.csv' | sort | head -n 1
}

find_attempts_csv() {
  local dir="$1"
  find "$dir" -maxdepth 4 -type f -name 'attempts.csv' | sort | head -n 1
}

# Pull metrics from summary.csv if present. Otherwise derive medians/means from attempts.csv.
summarize_point() {
  local depth="$1"
  local bb_each="$2"
  local point_dir="$3"
  local note="$4"

  local summary_csv attempts_csv
  summary_csv="$(find_summary_csv "$point_dir" || true)"
  attempts_csv="$(find_attempts_csv "$point_dir" || true)"

  printf '%s,%s,%s,%s,%s,%s\n' "$depth" "$bb_each" "$point_dir" "${summary_csv:-}" "${attempts_csv:-}" >> "$RAW_INDEX_CSV"

  python3 - "$depth" "$bb_each" "$RUNS_PER_POINT" "$point_dir" "$note" "$summary_csv" "$attempts_csv" >> "$AGG_CSV" <<'PY'
import csv, os, sys, statistics

depth, bb_each, runs, point_dir, note, summary_csv, attempts_csv = sys.argv[1:8]
runs = int(runs)

def f(x, default=""):
    if x is None:
        return default
    s = str(x).strip()
    return s if s != "" else default

def to_float(x):
    try:
        return float(str(x).strip())
    except Exception:
        return None

def to_int(x):
    try:
        return int(float(str(x).strip()))
    except Exception:
        return None

def median_or_blank(vals):
    vals = [v for v in vals if v is not None]
    if not vals:
        return ""
    return f"{statistics.median(vals):.3f}"

def mean_or_blank(vals):
    vals = [v for v in vals if v is not None]
    if not vals:
        return ""
    return f"{sum(vals)/len(vals):.3f}"

if summary_csv and os.path.exists(summary_csv):
    with open(summary_csv, "r", encoding="utf-8-sig", newline="") as fh:
        rows = list(csv.DictReader(fh))
    row = rows[0] if rows else {}
    pass_count = to_int(row.get("pass_count")) or to_int(row.get("ok_count")) or to_int(row.get("success_count"))
    if pass_count is None:
        pass_count = runs
    pass_rate = to_float(row.get("pass_rate"))
    if pass_rate is None and runs > 0:
        pass_rate = pass_count / runs
    out = [
        depth,
        bb_each,
        str(runs),
        str(pass_count),
        f"{pass_rate:.6f}" if pass_rate is not None else "",
        f(row.get("median_path_ms")),
        f(row.get("median_witness_ms")),
        f(row.get("median_prove_ms")),
        f(row.get("median_verify_ms")),
        f(row.get("avg_path_ms")),
        f(row.get("avg_witness_ms")),
        f(row.get("avg_prove_ms")),
        f(row.get("avg_verify_ms")),
        f(row.get("proof_bytes_median")),
        f(row.get("public_bytes_median")),
        point_dir,
        note,
    ]
    print(",".join(out))
    raise SystemExit(0)

path_vals = []
witness_vals = []
prove_vals = []
verify_vals = []
proof_bytes = []
public_bytes = []
pass_count = 0

if attempts_csv and os.path.exists(attempts_csv):
    with open(attempts_csv, "r", encoding="utf-8-sig", newline="") as fh:
        for row in csv.DictReader(fh):
            ok = row.get("ok")
            accepted = row.get("accepted")
            passed = False
            if ok is not None:
                try:
                    passed = int(float(str(ok).strip())) == 1
                except Exception:
                    passed = False
            elif accepted is not None:
                try:
                    passed = int(float(str(accepted).strip())) == 1
                except Exception:
                    passed = False

            if passed:
                pass_count += 1
                path_vals.append(to_float(row.get("path_ms") or row.get("path")))
                witness_vals.append(to_float(row.get("witness_ms") or row.get("witness")))
                prove_vals.append(to_float(row.get("prove_ms") or row.get("prove")))
                verify_vals.append(to_float(row.get("verify_ms") or row.get("verify")))
                proof_bytes.append(to_int(row.get("proof_bytes")))
                public_bytes.append(to_int(row.get("public_bytes") or row.get("public_input_bytes")))

pass_rate = (pass_count / runs) if runs > 0 else None
out = [
    depth,
    bb_each,
    str(runs),
    str(pass_count),
    f"{pass_rate:.6f}" if pass_rate is not None else "",
    median_or_blank(path_vals),
    median_or_blank(witness_vals),
    median_or_blank(prove_vals),
    median_or_blank(verify_vals),
    mean_or_blank(path_vals),
    mean_or_blank(witness_vals),
    mean_or_blank(prove_vals),
    mean_or_blank(verify_vals),
    median_or_blank(proof_bytes),
    median_or_blank(public_bytes),
    point_dir,
    note,
]
print(",".join(out))
PY
}

run_auth_point() {
  local depth="$1"
  local bb_each="$2"

  local point_dir="$OUTDIR/depth_${depth}/bb_each_${bb_each}"
  mkdir -p "$point_dir"

  log "run point depth=$depth bb_each=$bb_each runs=$RUNS_PER_POINT"

  local rc=0
  (
    cd "$ROOT"
    env \
      PROJECT_ROOT="$PROJECT_ROOT" \
      ROOT="$ROOT" \
      TREE_DEPTH="$depth" \
      TTSS_N="$TTSS_N" \
      TTSS_T="$TTSS_T" \
      BB_EACH="$bb_each" \
      RUNS="$RUNS_PER_POINT" \
      OUTDIR="$point_dir" \
      RESULTS_DIR="$point_dir" \
      TIMEOUT_MS="$TIMEOUT_MS" \
      REGISTER_WAIT_MS="$REGISTER_WAIT_MS" \
      PATH_WAIT_MS="$PATH_WAIT_MS" \
      ROOT_WAIT_MS="$ROOT_WAIT_MS" \
      ROOT_POLL_MS="$ROOT_POLL_MS" \
      bash "$AUTH_BATCH_SCRIPT"
  ) >"$point_dir/stdout.log" 2>"$point_dir/stderr.log" || rc=$?

  local note=""
  if [[ "$rc" -ne 0 ]]; then
    note="inner_script_failed_rc_${rc}"
    log "point failed depth=$depth bb_each=$bb_each rc=$rc"
    if [[ "$CONTINUE_ON_ERROR" != "1" ]]; then
      summarize_point "$depth" "$bb_each" "$point_dir" "$note"
      die "auth point failed and CONTINUE_ON_ERROR=0"
    fi
  fi

  summarize_point "$depth" "$bb_each" "$point_dir" "$note"

  if [[ "$KEEP_POINT_DIRS" != "1" ]]; then
    find "$point_dir" -type f \( -name '*.wtns' -o -name '*.zkey' -o -name '*.json' \) -delete || true
  fi
}

write_readme() {
  cat > "$OUTDIR/README.txt" <<EOF
run_N_zk_depth_scale.sh results

Parameters:
  ROOT=$ROOT
  START_STOP_SCRIPT=$START_STOP_SCRIPT
  AUTH_BATCH_SCRIPT=$AUTH_BATCH_SCRIPT
  DEPTH_LIST=$DEPTH_LIST
  BB_EACH_LIST=$BB_EACH_LIST
  RUNS_PER_POINT=$RUNS_PER_POINT
  TTSS_N=$TTSS_N
  TTSS_T=$TTSS_T

Outputs:
  aggregate.csv  - one row per (depth, bb_each)
  raw_index.csv  - located summary/attempt files per point
EOF
}

main() {
  write_readme

  local depth bb_each
  for depth in $(csv_to_lines "$DEPTH_LIST"); do
    depth="$(trim "$depth")"
    [[ -n "$depth" ]] || continue

    restart_for_depth "$depth"
    prepare_depth "$depth"

    for bb_each in $(csv_to_lines "$BB_EACH_LIST"); do
      bb_each="$(trim "$bb_each")"
      [[ -n "$bb_each" ]] || continue
      run_auth_point "$depth" "$bb_each"
    done

    if [[ "$DEPTH_SLEEP_SEC" -gt 0 ]]; then
      sleep "$DEPTH_SLEEP_SEC"
    fi
  done

  log "done"
  log "aggregate: $AGG_CSV"
}

main "$@"
