#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="${PROJECT_ROOT:-$(cd "$SCRIPT_DIR/.." && pwd)}"
ENV_FILE="${ENV_FILE:-$PROJECT_ROOT/run/zk_local_env.sh}"

if [ -f "$ENV_FILE" ]; then
  # shellcheck disable=SC1090
  source "$ENV_FILE"
fi

BIN="${BIN:-$PROJECT_ROOT/build/did_demo_zk}"
BASE_URL="${BASE_URL:-http://127.0.0.1:3000}"
PIRATE_URL="${PIRATE_URL:-http://127.0.0.1:4000}"
VERIFY_HEALTH="${DIDZK_VERIFY_SERVICE_HEALTH:-http://127.0.0.1:3400/health}"

RECOVER_CASE="${RECOVER_CASE:-legal}"
DEPTH="${DEPTH:-20}"
TIMEOUT_MS="${TIMEOUT_MS:-1000}"
REGISTER_WAIT_MS="${REGISTER_WAIT_MS:-1000}"
PATH_WAIT_MS="${PATH_WAIT_MS:-1000}"
ROOT_WAIT_MS="${ROOT_WAIT_MS:-1000}"
ROOT_POLL_MS="${ROOT_POLL_MS:-500}"
ATTEMPTS="${ATTEMPTS:-100}"
CONTINUE_ON_FAIL="${CONTINUE_ON_FAIL:-0}"
SLEEP_BETWEEN_MS="${SLEEP_BETWEEN_MS:-0}"
RUN_TAG="${RUN_TAG:-$(date +%Y%m%d_%H%M%S)_$$}"

OUT="${OUT:-$PROJECT_ROOT/results/$(date +%Y%m%d_%H%M%S)_H_zk_recovery_batch}"
RUN_LOG="$OUT/run.log"
WORKDIR="$OUT/workdir"
PER_RUN_DIR="$OUT/per_run"
BATCH_CSV="$OUT/zk_recovery_batch.csv"
SUMMARY_TXT="$OUT/summary.txt"

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || {
    echo "missing command: $1" >&2
    exit 1
  }
}

need_file() {
  [ -f "$1" ] || {
    echo "missing file: $1" >&2
    exit 1
  }
}

log() {
  echo "$1" | tee -a "$RUN_LOG"
}

check_health() {
  local url="$1"
  local name="$2"
  if ! curl -fsS "$url" >/dev/null 2>&1; then
    echo "health check failed: $name ($url)" >&2
    exit 1
  fi
}

sleep_ms() {
  local ms="$1"
  if [ "$ms" -gt 0 ] 2>/dev/null; then
    python3 - "$ms" <<'PY'
import sys, time
ms = int(sys.argv[1])
time.sleep(ms / 1000.0)
PY
  fi
}

validate_recovery() {
  local csv_path="$1"
  local result_json="$2"
  local old_root_json="$3"
  local new_root_json="$4"
  local run_id="$5"
  python3 - "$csv_path" "$result_json" "$old_root_json" "$new_root_json" "$run_id" <<'PY'
import csv
import json
import sys

csv_path, result_json, old_root_json, new_root_json, run_id = sys.argv[1:6]

with open(csv_path, newline='', encoding='utf-8') as f:
    reader = csv.DictReader(f)
    if not reader.fieldnames:
        raise SystemExit(f"validation failed: empty csv header: {csv_path}")
    required = ['id', 'recover_case', 'old_proof_valid', 'new_proof_valid', 'ok']
    missing = [k for k in required if k not in reader.fieldnames]
    if missing:
        raise SystemExit(f"validation failed: missing columns {missing} in {csv_path}")
    rows = list(reader)

if not rows:
    raise SystemExit(f"validation failed: no data rows in {csv_path}")

row = rows[-1]
if row.get('id') != run_id:
    raise SystemExit(f"validation failed: csv id={row.get('id')} expected {run_id}")
if row.get('old_proof_valid') != '0':
    raise SystemExit(f"validation failed: old_proof_valid={row.get('old_proof_valid')} expected 0")
if row.get('new_proof_valid') != '1':
    raise SystemExit(f"validation failed: new_proof_valid={row.get('new_proof_valid')} expected 1")
if row.get('ok') != '1':
    raise SystemExit(f"validation failed: ok={row.get('ok')} expected 1")

with open(result_json, encoding='utf-8') as f:
    result = json.load(f)
with open(old_root_json, encoding='utf-8') as f:
    old_root = json.load(f)
with open(new_root_json, encoding='utf-8') as f:
    new_root = json.load(f)

if result.get('id') not in (None, run_id):
    raise SystemExit(f"validation failed: result.json id={result.get('id')} expected {run_id}")
if bool(result.get('old_proof_valid')):
    raise SystemExit('validation failed: result.json old_proof_valid should be false')
if not bool(result.get('new_proof_valid')):
    raise SystemExit('validation failed: result.json new_proof_valid should be true')
if int(result.get('ok', 0)) != 1:
    raise SystemExit('validation failed: result.json ok should be 1')

if old_root.get('root') == new_root.get('root') and old_root.get('epoch') == new_root.get('epoch'):
    raise SystemExit('validation failed: root/epoch did not change after recovery')

print('[run_H_batch][check] recovery csv/result/root validation passed')
print(f"[run_H_batch][check] run_id={run_id}")
print(f"[run_H_batch][check] old_root={old_root.get('root')} epoch={old_root.get('epoch')}")
print(f"[run_H_batch][check] new_root={new_root.get('root')} epoch={new_root.get('epoch')}")
PY
}

append_last_row() {
  local attempt="$1"
  local src_csv="$2"
  local dst_csv="$3"
  python3 - "$attempt" "$src_csv" "$dst_csv" <<'PY'
import csv
import os
import sys

attempt, src_csv, dst_csv = sys.argv[1:4]
with open(src_csv, newline='', encoding='utf-8') as f:
    reader = csv.DictReader(f)
    rows = list(reader)
    if not rows:
        raise SystemExit(f"append failed: no rows in {src_csv}")
    fieldnames = ['attempt'] + list(reader.fieldnames or [])
    row = rows[-1]
    row_out = {'attempt': attempt}
    row_out.update(row)

write_header = not os.path.exists(dst_csv)
with open(dst_csv, 'a', newline='', encoding='utf-8') as f:
    writer = csv.DictWriter(f, fieldnames=fieldnames)
    if write_header:
        writer.writeheader()
    writer.writerow(row_out)
PY
}

write_summary() {
  local batch_csv="$1"
  local summary_txt="$2"
  python3 - "$batch_csv" "$summary_txt" <<'PY'
import csv
import statistics
import sys
from pathlib import Path

batch_csv, summary_txt = sys.argv[1:3]
rows = []
with open(batch_csv, newline='', encoding='utf-8') as f:
    reader = csv.DictReader(f)
    rows = list(reader)

if not rows:
    raise SystemExit('summary failed: empty batch csv')

num_rows = len(rows)
ok_rows = [r for r in rows if r.get('ok') == '1']
old_invalid = sum(1 for r in rows if r.get('old_proof_valid') == '0')
new_valid = sum(1 for r in rows if r.get('new_proof_valid') == '1')

def collect_float(key):
    vals = []
    for r in rows:
        v = r.get(key, '')
        try:
            vals.append(float(v))
        except Exception:
            pass
    return vals

keys = [
    'recover_ms', 'rotate_ms', 'prove_old_ms', 'prove_new_ms',
    'verify_old_ms', 'verify_new_ms', 'path_fetch_old_ms', 'path_fetch_new_ms'
]
lines = []
lines.append(f"attempts={num_rows}")
lines.append(f"ok_rows={len(ok_rows)}")
lines.append(f"old_proof_invalid_rows={old_invalid}")
lines.append(f"new_proof_valid_rows={new_valid}")
for key in keys:
    vals = collect_float(key)
    if vals:
        lines.append(
            f"{key}: mean={statistics.fmean(vals):.3f}, "
            f"median={statistics.median(vals):.3f}, "
            f"min={min(vals):.3f}, max={max(vals):.3f}"
        )

Path(summary_txt).write_text("\n".join(lines) + "\n", encoding='utf-8')
print("\n".join(lines))
PY
}

need_cmd tee
need_cmd curl
need_cmd python3
need_file "$BIN"

mkdir -p "$OUT" "$WORKDIR" "$PER_RUN_DIR"

log "[run_H_batch] PROJECT_ROOT=$PROJECT_ROOT"
log "[run_H_batch] BIN=$BIN"
log "[run_H_batch] BASE_URL=$BASE_URL"
log "[run_H_batch] PIRATE_URL=$PIRATE_URL"
log "[run_H_batch] VERIFY_HEALTH=$VERIFY_HEALTH"
log "[run_H_batch] RECOVER_CASE=$RECOVER_CASE"
log "[run_H_batch] DEPTH=$DEPTH"
log "[run_H_batch] ATTEMPTS=$ATTEMPTS"
log "[run_H_batch] CONTINUE_ON_FAIL=$CONTINUE_ON_FAIL"
log "[run_H_batch] RUN_TAG=$RUN_TAG"
log "[run_H_batch] OUT=$OUT"

check_health "$BASE_URL/health" "bb_service_zk"
check_health "$VERIFY_HEALTH" "zk_verify_service"

pass_count=0
fail_count=0

for attempt in $(seq 1 "$ATTEMPTS"); do
  attempt_tag=$(printf "%03d" "$attempt")
  run_id="zk_recover_${RUN_TAG}_${attempt_tag}"
  run_csv="$PER_RUN_DIR/${run_id}.csv"
  result_dir="$WORKDIR/${run_id}_recovery"

  log "[run_H_batch] attempt=$attempt/$ATTEMPTS run_id=$run_id"

  if "$BIN" --zk_recovery_e2e \
    --id "$run_id" \
    --bb "$BASE_URL" \
    --pirate "$PIRATE_URL" \
    --recover_case "$RECOVER_CASE" \
    --depth "$DEPTH" \
    --timeout_ms "$TIMEOUT_MS" \
    --register_wait_ms "$REGISTER_WAIT_MS" \
    --path_wait_ms "$PATH_WAIT_MS" \
    --root_wait_ms "$ROOT_WAIT_MS" \
    --root_poll_ms "$ROOT_POLL_MS" \
    --project_root "$PROJECT_ROOT" \
    --workdir "$WORKDIR" \
    --csv "$run_csv" \
    >> "$RUN_LOG" 2>&1; then
    need_file "$run_csv"
    need_file "$result_dir/result.json"
    need_file "$result_dir/old_root.json"
    need_file "$result_dir/new_root.json"

    if validate_recovery "$run_csv" "$result_dir/result.json" "$result_dir/old_root.json" "$result_dir/new_root.json" "$run_id" >> "$RUN_LOG" 2>&1; then
      append_last_row "$attempt" "$run_csv" "$BATCH_CSV"
      pass_count=$((pass_count + 1))
      log "[run_H_batch] PASS attempt=$attempt run_id=$run_id"
    else
      fail_count=$((fail_count + 1))
      log "[run_H_batch] FAIL validation attempt=$attempt run_id=$run_id"
      if [ "$CONTINUE_ON_FAIL" != "1" ]; then
        exit 1
      fi
    fi
  else
    fail_count=$((fail_count + 1))
    log "[run_H_batch] FAIL execution attempt=$attempt run_id=$run_id"
    if [ "$CONTINUE_ON_FAIL" != "1" ]; then
      exit 1
    fi
  fi

  sleep_ms "$SLEEP_BETWEEN_MS"
done

need_file "$BATCH_CSV"
write_summary "$BATCH_CSV" "$SUMMARY_TXT" | tee -a "$RUN_LOG"

log "[run_H_batch] DONE"
log "[run_H_batch] pass_count=$pass_count fail_count=$fail_count"
log "[run_H_batch] outputs:"
log "  $BATCH_CSV"
log "  $SUMMARY_TXT"
log "  $PER_RUN_DIR"
log "  $WORKDIR"
log "  $RUN_LOG"
