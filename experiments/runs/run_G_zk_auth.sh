#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

log() { echo "$*"; }
die() { echo "$*" >&2; exit 1; }

PROJECT_ROOT="${PROJECT_ROOT:-$(cd "$SCRIPT_DIR/../.." && pwd)}"
BIN="${BIN:-$PROJECT_ROOT/build/did_demo_zk}"
BASE_URL="${BASE_URL:-http://127.0.0.1:3000}"
VERIFY_HEALTH="${VERIFY_HEALTH:-http://127.0.0.1:3400/health}"

DEPTH="${DEPTH:-20}"
RUNS_BB1="${RUNS_BB1:-5}"
RUNS_BB0="${RUNS_BB0:-0}"

# Relaxed wait parameters to reduce false negatives around registerStatus/root/path readiness.
TIMEOUT_MS="${TIMEOUT_MS:-30000}"
REGISTER_WAIT_MS="${REGISTER_WAIT_MS:-120000}"
PATH_WAIT_MS="${PATH_WAIT_MS:-120000}"
ROOT_WAIT_MS="${ROOT_WAIT_MS:-60000}"
ROOT_POLL_MS="${ROOT_POLL_MS:-300}"

RUN_TAG="${RUN_TAG:-$(date +%Y%m%d_%H%M%S)_$$}"
OUT="${OUT:-$PROJECT_ROOT/results/$(date +%Y%m%d_%H%M%S)_G_zk_auth}"

mkdir -p "$OUT"

[[ -x "$BIN" ]] || die "[run_G] FATAL: BIN not found or not executable: $BIN"

health_check() {
  log "[run_G] health check: bb"
  curl -fsS "$BASE_URL/health" >/dev/null || die "[run_G] FATAL: bb health failed: $BASE_URL/health"

  log "[run_G] health check: verifier"
  curl -fsS "$VERIFY_HEALTH" >/dev/null || die "[run_G] FATAL: verifier health failed: $VERIFY_HEALTH"
}

run_one() {
  local mode="$1"
  local runs="$2"
  local run_id="zk_auth_bb${mode}_${RUN_TAG}"
  local csv="$OUT/zk_auth_bb${mode}.csv"
  local workdir="$OUT/workdir_bb${mode}"

  mkdir -p "$workdir"

  log "[run_G] >>> mode=$mode runs=$runs run_id=$run_id csv=$csv"
  if (( runs <= 0 )); then
    log "[run_G] skip mode=$mode because runs=$runs"
    return 0
  fi

  "$BIN" --zk_auth_e2e \
    --id "$run_id" \
    --bb "$BASE_URL" \
    --runs "$runs" \
    --bb_each "$mode" \
    --depth "$DEPTH" \
    --timeout_ms "$TIMEOUT_MS" \
    --register_wait_ms "$REGISTER_WAIT_MS" \
    --path_wait_ms "$PATH_WAIT_MS" \
    --root_wait_ms "$ROOT_WAIT_MS" \
    --root_poll_ms "$ROOT_POLL_MS" \
    --project_root "$PROJECT_ROOT" \
    --workdir "$workdir" \
    --csv "$csv"
}

print_summary() {
  local csv="$1"
  local label="$2"

  [[ -f "$csv" ]] || {
    log "[run_G] WARN: summary skipped, csv not found: $csv"
    return 0
  }

  python3 - "$csv" "$label" <<'PY'
import csv, statistics, sys

csv_path = sys.argv[1]
label = sys.argv[2]

rows = []
with open(csv_path, "r", encoding="utf-8-sig", newline="") as f:
    reader = csv.DictReader(f)
    for r in reader:
        rows.append(r)

def to_float(v):
    try:
        return float(str(v).strip())
    except Exception:
        return None

def to_int(v):
    try:
        return int(float(str(v).strip()))
    except Exception:
        return None

ok_rows = []
for r in rows:
    ok = r.get("ok")
    if ok is None:
        ok = r.get("accepted")
    try:
        passed = int(float(str(ok).strip())) == 1
    except Exception:
        passed = False
    if passed:
        ok_rows.append(r)

path_vals = [to_float(r.get("path_ms") or r.get("path")) for r in ok_rows]
wit_vals = [to_float(r.get("witness_ms") or r.get("witness")) for r in ok_rows]
prove_vals = [to_float(r.get("prove_ms") or r.get("prove")) for r in ok_rows]
verify_vals = [to_float(r.get("verify_ms") or r.get("verify")) for r in ok_rows]
proof_bytes = [to_int(r.get("proof_bytes")) for r in ok_rows]
public_bytes = [to_int(r.get("public_bytes") or r.get("public_input_bytes")) for r in ok_rows]

def clean(vals):
    return [v for v in vals if v is not None]

def median(vals):
    vals = clean(vals)
    return statistics.median(vals) if vals else None

def mean(vals):
    vals = clean(vals)
    return sum(vals) / len(vals) if vals else None

def fmt(v):
    return "-" if v is None else f"{v:.1f}"

print(f"[run_G] summary {label}: total={len(rows)} ok={len(ok_rows)}")
print(f"[run_G] summary {label}: median path={fmt(median(path_vals))} ms witness={fmt(median(wit_vals))} ms prove={fmt(median(prove_vals))} ms verify={fmt(median(verify_vals))} ms")
print(f"[run_G] summary {label}: avg    path={fmt(mean(path_vals))} ms witness={fmt(mean(wit_vals))} ms prove={fmt(mean(prove_vals))} ms verify={fmt(mean(verify_vals))} ms")

pb = median(proof_bytes)
ub = median(public_bytes)
if pb is not None or ub is not None:
    print(f"[run_G] summary {label}: proof_bytes_median={pb if pb is not None else '-'} public_bytes_median={ub if ub is not None else '-'}")
PY
}

main() {
  health_check

  log "[run_G] PROJECT_ROOT=$PROJECT_ROOT"
  log "[run_G] BIN=$BIN"
  log "[run_G] BASE_URL=$BASE_URL"
  log "[run_G] VERIFY_HEALTH=$VERIFY_HEALTH"
  log "[run_G] DEPTH=$DEPTH"
  log "[run_G] TIMEOUT_MS=$TIMEOUT_MS"
  log "[run_G] REGISTER_WAIT_MS=$REGISTER_WAIT_MS"
  log "[run_G] PATH_WAIT_MS=$PATH_WAIT_MS"
  log "[run_G] ROOT_WAIT_MS=$ROOT_WAIT_MS"
  log "[run_G] ROOT_POLL_MS=$ROOT_POLL_MS"
  log "[run_G] RUN_TAG=$RUN_TAG"
  log "[run_G] OUT=$OUT"

  run_one 1 "$RUNS_BB1"
  print_summary "$OUT/zk_auth_bb1.csv" "bb_each=1"

  run_one 0 "$RUNS_BB0"
  print_summary "$OUT/zk_auth_bb0.csv" "bb_each=0"

  log "[run_G] done"
}

main "$@"
