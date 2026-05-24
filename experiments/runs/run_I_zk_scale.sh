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
VERIFY_HEALTH="${DIDZK_VERIFY_SERVICE_HEALTH:-http://127.0.0.1:3400/health}"
DEPTHS="${DEPTHS:-16 20 24 28}"
RUNS_PER_DEPTH="${RUNS_PER_DEPTH:-3}"
BB_EACH="${BB_EACH:-1}"
TIMEOUT_MS="${TIMEOUT_MS:-1000}"
RUN_TAG="${RUN_TAG:-$(date +%Y%m%d_%H%M%S)_$$}"

OUT="${OUT:-$PROJECT_ROOT/results/$(date +%Y%m%d_%H%M%S)_I_zk_scale}"
RUN_LOG="$OUT/run.log"

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

validate_scale_csv() {
  local csv_path="$1"
  local expected_depth="$2"
  local expected_bb_each="$3"
  local run_tag="$4"
  python3 - "$csv_path" "$expected_depth" "$expected_bb_each" "$run_tag" <<'PY'
import csv
import sys
from statistics import mean

csv_path, expected_depth, expected_bb_each, run_tag = sys.argv[1:5]

with open(csv_path, newline='', encoding='utf-8') as f:
    reader = csv.DictReader(f)
    if not reader.fieldnames:
        raise SystemExit(f"validation failed: empty csv header: {csv_path}")
    required = ['id', 'depth', 'bb_each', 'prove_ms', 'verify_ms', 'ok']
    missing = [k for k in required if k not in reader.fieldnames]
    if missing:
        raise SystemExit(f"validation failed: missing columns {missing} in {csv_path}")
    rows = list(reader)

if not rows:
    raise SystemExit(f"validation failed: no data rows in {csv_path}")

bad = []
matched_tag = 0
for idx, row in enumerate(rows, start=2):
    row_id = row.get('id', '')
    if row.get('depth') != expected_depth:
        bad.append(f"line {idx}: depth={row.get('depth')} expected {expected_depth}")
    if row.get('bb_each') != expected_bb_each:
        bad.append(f"line {idx}: bb_each={row.get('bb_each')} expected {expected_bb_each}")
    if row.get('ok') != '1':
        bad.append(f"line {idx}: ok={row.get('ok')} expected 1")
    if run_tag and run_tag in row_id:
        matched_tag += 1

if bad:
    preview = '; '.join(bad[:10])
    raise SystemExit(f"validation failed: {csv_path}: {preview}")

if matched_tag == 0:
    raise SystemExit(f"validation failed: no row id contains current RUN_TAG={run_tag} in {csv_path}")

prove = [float(r['prove_ms']) for r in rows]
verify = [float(r['verify_ms']) for r in rows]
print(f"[run_I][check] {csv_path} rows={len(rows)} avg_prove_ms={mean(prove):.3f} avg_verify_ms={mean(verify):.3f} run_tag_matches={matched_tag}")
PY
}

need_cmd tee
need_cmd curl
need_cmd python3
need_file "$BIN"

mkdir -p "$OUT"

log "[run_I] PROJECT_ROOT=$PROJECT_ROOT"
log "[run_I] BIN=$BIN"
log "[run_I] BASE_URL=$BASE_URL"
log "[run_I] VERIFY_HEALTH=$VERIFY_HEALTH"
log "[run_I] DEPTHS=$DEPTHS"
log "[run_I] RUNS_PER_DEPTH=$RUNS_PER_DEPTH"
log "[run_I] BB_EACH=$BB_EACH"
log "[run_I] RUN_TAG=$RUN_TAG"
log "[run_I] OUT=$OUT"

check_health "$BASE_URL/health" "bb_service_zk"
check_health "$VERIFY_HEALTH" "zk_verify_service"

for DEPTH in $DEPTHS; do
  RUN_ID="zk_scale_${DEPTH}_${RUN_TAG}"
  CSV_PATH="$OUT/depth_${DEPTH}.csv"
  WORKDIR="$OUT/workdir_depth_${DEPTH}"

  mkdir -p "$WORKDIR"
  log "[run_I] >>> depth=$DEPTH runs=$RUNS_PER_DEPTH run_id=$RUN_ID csv=$CSV_PATH"

  "$BIN" --zk_auth_e2e \
    --id "$RUN_ID" \
    --bb "$BASE_URL" \
    --runs "$RUNS_PER_DEPTH" \
    --depth "$DEPTH" \
    --bb_each "$BB_EACH" \
    --timeout_ms "$TIMEOUT_MS" \
    --project_root "$PROJECT_ROOT" \
    --workdir "$WORKDIR" \
    --csv "$CSV_PATH" \
    2>&1 | tee -a "$RUN_LOG"

  validate_scale_csv "$CSV_PATH" "$DEPTH" "$BB_EACH" "$RUN_TAG" | tee -a "$RUN_LOG"
done

log "[run_I] PASS"
log "[run_I] outputs:"
for DEPTH in $DEPTHS; do
  log "  $OUT/depth_${DEPTH}.csv"
done
log "  $RUN_LOG"
