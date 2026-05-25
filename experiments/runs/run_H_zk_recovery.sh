#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="${PROJECT_ROOT:-$(cd "$SCRIPT_DIR/../.." && pwd)}"
ENV_FILE="${ENV_FILE:-$PROJECT_ROOT/run/ttss_phase5_env.sh}"

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
TIMEOUT_MS="${TIMEOUT_MS:-30000}"
REGISTER_WAIT_MS="${REGISTER_WAIT_MS:-120000}"
PATH_WAIT_MS="${PATH_WAIT_MS:-120000}"
ROOT_WAIT_MS="${ROOT_WAIT_MS:-60000}"
ROOT_POLL_MS="${ROOT_POLL_MS:-300}"
RUN_TAG="${RUN_TAG:-$(date +%Y%m%d_%H%M%S)_$$}"
RUN_ID="${RUN_ID:-zk_recover_${RUN_TAG}}"

OUT="${OUT:-$PROJECT_ROOT/results/$(date +%Y%m%d_%H%M%S)_H_zk_recovery}"
RUN_LOG="$OUT/run.log"
CSV_PATH="$OUT/zk_recovery.csv"
WORKDIR="$OUT/workdir"
RESULT_DIR="$WORKDIR/${RUN_ID}_recovery"

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

print('[run_H][check] recovery csv/result/root validation passed')
print(f"[run_H][check] run_id={run_id}")
print(f"[run_H][check] old_root={old_root.get('root')} epoch={old_root.get('epoch')}")
print(f"[run_H][check] new_root={new_root.get('root')} epoch={new_root.get('epoch')}")
PY
}

need_cmd tee
need_cmd curl
need_cmd python3
need_file "$BIN"

mkdir -p "$OUT" "$WORKDIR"

log "[run_H] PROJECT_ROOT=$PROJECT_ROOT"
log "[run_H] BIN=$BIN"
log "[run_H] BASE_URL=$BASE_URL"
log "[run_H] PIRATE_URL=$PIRATE_URL"
log "[run_H] VERIFY_HEALTH=$VERIFY_HEALTH"
log "[run_H] RECOVER_CASE=$RECOVER_CASE"
log "[run_H] DEPTH=$DEPTH"
log "[run_H] RUN_TAG=$RUN_TAG"
log "[run_H] RUN_ID=$RUN_ID"
log "[run_H] OUT=$OUT"

check_health "$BASE_URL/health" "bb_service_zk"
check_health "$VERIFY_HEALTH" "zk_verify_service"

"$BIN" --zk_recovery_e2e \
  --id "$RUN_ID" \
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
  --csv "$CSV_PATH" \
  2>&1 | tee -a "$RUN_LOG"

need_file "$CSV_PATH"
need_file "$RESULT_DIR/result.json"
need_file "$RESULT_DIR/old_root.json"
need_file "$RESULT_DIR/new_root.json"

validate_recovery "$CSV_PATH" "$RESULT_DIR/result.json" "$RESULT_DIR/old_root.json" "$RESULT_DIR/new_root.json" "$RUN_ID" | tee -a "$RUN_LOG"

log "[run_H] PASS"
log "[run_H] outputs:"
log "  $CSV_PATH"
log "  $RESULT_DIR/result.json"
log "  $RESULT_DIR/old_root.json"
log "  $RESULT_DIR/new_root.json"
log "  $RUN_LOG"
