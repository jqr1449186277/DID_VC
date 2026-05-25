#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

ROOT="${ROOT:-${PROJECT_ROOT:-$(cd "$SCRIPT_DIR/../.." && pwd)}}"
BIN="${BIN:-$ROOT/build/did_demo_zk}"
OUT_BASE="${OUT_BASE:-$ROOT/results}"
BB="${BB:-http://127.0.0.1:3000}"
TOKEN="${TOKEN:-demo-token}"
COMMITTEE_URLS="${COMMITTEE_URLS:-http://127.0.0.1:8001,http://127.0.0.1:8002,http://127.0.0.1:8003,http://127.0.0.1:8004,http://127.0.0.1:8005,http://127.0.0.1:8006}"
RUNS="${RUNS:-5}"
MAX_RETRIES="${MAX_RETRIES:-2}"
RETRY_SLEEP_SEC="${RETRY_SLEEP_SEC:-2}"
TTSS_N="${TTSS_N:-6}"
TTSS_T="${TTSS_T:-4}"
PROJECT_ROOT="${PROJECT_ROOT:-$ROOT}"
TIMEOUT_MS="${TIMEOUT_MS:-30000}"
REGISTER_WAIT_MS="${REGISTER_WAIT_MS:-120000}"
PATH_WAIT_MS="${PATH_WAIT_MS:-120000}"
ROOT_WAIT_MS="${ROOT_WAIT_MS:-60000}"
ROOT_POLL_MS="${ROOT_POLL_MS:-300}"
BB_ASYNC_SUBMIT="${BB_ASYNC_SUBMIT:-0}"

TS="$(date +%Y%m%d_%H%M%S)"
OUT_DIR="${OUT_DIR:-$OUT_BASE/${TS}_J_ttss_threshold_boundary}"
mkdir -p "$OUT_DIR"
RUN_LOG="$OUT_DIR/run.log"
ATTEMPTS_CSV="$OUT_DIR/attempts.csv"
SUMMARY_TXT="$OUT_DIR/summary.txt"
CONFIG_JSON="$OUT_DIR/config.json"

log(){ echo "[run_J_threshold_boundary] $*" | tee -a "$RUN_LOG"; }
die(){ log "FATAL: $*"; exit 1; }
require_cmd(){ command -v "$1" >/dev/null 2>&1 || die "missing command: $1"; }

require_cmd jq
require_cmd curl
require_cmd python3
[[ -x "$BIN" ]] || die "BIN not executable: $BIN"

if [[ -z "${BOUNDARY_COUNTS:-}" ]]; then
  low=$((TTSS_T - 1))
  (( low < 1 )) && low=1
  high=$((TTSS_T + 1))
  (( high > TTSS_N )) && high="$TTSS_N"
  BOUNDARY_COUNTS="$low,$TTSS_T,$high"
fi

now_ms(){ date +%s%3N; }
ms_diff(){ python3 - "$1" "$2" <<'PY'
import sys
print(max(0, int(sys.argv[2]) - int(sys.argv[1])))
PY
}
json_get(){ jq -r "$2 // empty" "$1"; }
json_num(){ jq -r "$2 // 0" "$1"; }

append_csv(){
  local csv="$1"; shift
  python3 - "$csv" "$@" <<'PY'
import csv, sys
with open(sys.argv[1], 'a', newline='') as f:
    csv.writer(f).writerow(sys.argv[2:])
PY
}

check_health(){
  log "health check: bb"
  curl -fsS "$BB/health" >/dev/null || die "bb health failed: $BB/health"
  log "health check: committee"
  IFS=',' read -r -a committee_arr <<< "$COMMITTEE_URLS"
  local u
  for u in "${committee_arr[@]}"; do
    curl -fsS "$u/health" >/dev/null || die "committee health failed: $u/health"
  done
  [[ "${#committee_arr[@]}" -ge "$TTSS_N" ]] || die "TTSS_N=$TTSS_N exceeds committee url count=${#committee_arr[@]}"
  [[ "$TTSS_T" -le "$TTSS_N" ]] || die "TTSS_T must be <= TTSS_N"
}

jq -nc \
  --arg root "$ROOT" \
  --arg bin "$BIN" \
  --arg bb "$BB" \
  --arg token "$TOKEN" \
  --arg committee_urls "$COMMITTEE_URLS" \
  --arg boundary_counts "$BOUNDARY_COUNTS" \
  --argjson runs "$RUNS" \
  --argjson max_retries "$MAX_RETRIES" \
  --argjson ttss_n "$TTSS_N" \
  --argjson ttss_t "$TTSS_T" \
  '{root:$root,bin:$bin,bb:$bb,token:$token,committee_urls:$committee_urls,boundary_counts:$boundary_counts,runs:$runs,max_retries:$max_retries,ttss_n:$ttss_n,ttss_t:$ttss_t}' > "$CONFIG_JSON"

cat > "$ATTEMPTS_CSV" <<'CSV'
run_no,attempt_no,case_label,requested_share_count,system_n,system_t,expected_success,status,pass,setup_ok,recover_ok,id,id_hash,ver,epoch,setup_ms,recover_ms,recovered_matches_expected,share_count_used,recovered_seed_hex,expected_seed_hex,err_contains_insufficient,fail_stage,err,setup_json,recover_json,case_dir
CSV

run_one_case(){
  local run_no="$1" attempt_no="$2" requested_count="$3" case_dir="$4" setup_json="$5"
  mkdir -p "$case_dir"
  local case_label="shares_${requested_count}"
  local id id_hash ver epoch expected_seed status="fail" pass="0" setup_ok="1" recover_ok="0"
  local setup_ms="0" recover_ms="0" recovered_matches_expected="" share_count_used="0" recovered_seed_hex="" expected_seed_hex="" err_contains_insufficient="0" fail_stage="" err="" recover_json=""
  id="$(json_get "$setup_json" '.id')"
  id_hash="$(json_get "$setup_json" '.idHash')"
  ver="$(json_get "$setup_json" '.ver')"
  epoch="$(json_get "$setup_json" '.epoch')"
  expected_seed="$(json_get "$setup_json" '.localKeys.boardSeeds.recoverySeedHex')"
  setup_ms="$(json_num "$setup_json" '.timings.setup_total_inner_ms')"
  expected_seed_hex="$expected_seed"

  local expected_success="0"
  if (( requested_count >= TTSS_T )); then expected_success="1"; fi

  local recover_setup_json="$case_dir/setup_for_recover.json"
  jq --argjson requested_count "$requested_count" '.t = $requested_count' "$setup_json" > "$recover_setup_json"

  local start end
  start="$(now_ms)"
  if "$BIN" --ttss_recover \
      --id "$id" \
      --bb "$BB" \
      --committee_urls "$COMMITTEE_URLS" \
      --committee_token "$TOKEN" \
      --ttss_n "$TTSS_N" \
      --ttss_t "$TTSS_T" \
      --ttss_state "$recover_setup_json" \
      --project_root "$PROJECT_ROOT" \
      --workdir "$case_dir" \
      --timeout_ms "$TIMEOUT_MS" \
      --register_wait_ms "$REGISTER_WAIT_MS" \
      --path_wait_ms "$PATH_WAIT_MS" \
      --root_wait_ms "$ROOT_WAIT_MS" \
      --root_poll_ms "$ROOT_POLL_MS" \
      --bb_async_submit "$BB_ASYNC_SUBMIT" >"$case_dir/ttss_recover.out" 2>"$case_dir/ttss_recover.err"; then
    end="$(now_ms)"; recover_ms="$(ms_diff "$start" "$end")"
    recover_json="$case_dir/${id}_ttss_recover/recover_result.json"
    [[ -f "$recover_json" ]] || { fail_stage="recover_json_missing"; err="missing $recover_json"; append_csv "$ATTEMPTS_CSV" "$run_no" "$attempt_no" "$case_label" "$requested_count" "$TTSS_N" "$TTSS_T" "$expected_success" "$status" "$pass" "$setup_ok" "$recover_ok" "$id" "$id_hash" "$ver" "$epoch" "$setup_ms" "$recover_ms" "$recovered_matches_expected" "$share_count_used" "$recovered_seed_hex" "$expected_seed_hex" "$err_contains_insufficient" "$fail_stage" "$err" "$setup_json" "$recover_json" "$case_dir"; return 0; }
    recovered_matches_expected="$(json_get "$recover_json" '.recoveredMatchesExpected')"
    share_count_used="$(json_num "$recover_json" '.shareCountUsed')"
    recovered_seed_hex="$(json_get "$recover_json" '.recoveredSeedHex')"
    recover_ok="1"
    if [[ "$expected_success" == "1" && ( "$recovered_matches_expected" == "true" || "$recovered_matches_expected" == "1" ) ]]; then
      status="pass"; pass="1"
    else
      fail_stage="unexpected_success"
      err="recover succeeded when failure expected"
    fi
  else
    end="$(now_ms)"; recover_ms="$(ms_diff "$start" "$end")"
    err="$(tr '\n' ' ' < "$case_dir/ttss_recover.err" | tail -c 500)"
    if grep -qi 'insufficient_shares' "$case_dir/ttss_recover.err"; then err_contains_insufficient="1"; fi
    if [[ "$expected_success" == "0" && "$err_contains_insufficient" == "1" ]]; then
      status="pass"; pass="1"
    else
      fail_stage="unexpected_failure"
      [[ -z "$err" ]] && err="ttss_recover failed"
    fi
  fi

  if [[ "$status" == "pass" && "$expected_success" == "1" ]]; then
    # When using > t shares, the implementation still reconstructs with the embedded system threshold.
    if [[ "$share_count_used" != "$TTSS_T" ]]; then
      status="fail"; pass="0"; fail_stage="share_count_used"; err="shareCountUsed=$share_count_used expected_system_t=$TTSS_T"
    fi
  fi

  append_csv "$ATTEMPTS_CSV" \
    "$run_no" "$attempt_no" "$case_label" "$requested_count" "$TTSS_N" "$TTSS_T" "$expected_success" "$status" "$pass" "$setup_ok" "$recover_ok" "$id" "$id_hash" "$ver" "$epoch" "$setup_ms" "$recover_ms" "$recovered_matches_expected" "$share_count_used" "$recovered_seed_hex" "$expected_seed_hex" "$err_contains_insufficient" "$fail_stage" "$err" "$setup_json" "$recover_json" "$case_dir"
}

run_one_attempt(){
  local run_no="$1" attempt_no="$2" base_dir="$3"
  mkdir -p "$base_dir"
  local id="boundary_r${run_no}_a${attempt_no}_$(date +%s)"
  log "run=$run_no attempt=$attempt_no ttss_setup id=$id"
  if ! "$BIN" --ttss_setup \
      --id "$id" \
      --bb "$BB" \
      --committee_urls "$COMMITTEE_URLS" \
      --committee_token "$TOKEN" \
      --ttss_n "$TTSS_N" \
      --ttss_t "$TTSS_T" \
      --project_root "$PROJECT_ROOT" \
      --workdir "$base_dir" \
      --timeout_ms "$TIMEOUT_MS" \
      --register_wait_ms "$REGISTER_WAIT_MS" \
      --path_wait_ms "$PATH_WAIT_MS" \
      --root_wait_ms "$ROOT_WAIT_MS" \
      --root_poll_ms "$ROOT_POLL_MS" \
      --bb_async_submit "$BB_ASYNC_SUBMIT" >"$base_dir/ttss_setup.out" 2>"$base_dir/ttss_setup.err"; then
    local setup_err
    setup_err="$(tr '\n' ' ' < "$base_dir/ttss_setup.err" | tail -c 500)"
    for requested_count in $(echo "$BOUNDARY_COUNTS" | tr ',' ' '); do
      append_csv "$ATTEMPTS_CSV" "$run_no" "$attempt_no" "shares_${requested_count}" "$requested_count" "$TTSS_N" "$TTSS_T" "" "fail" "0" "0" "0" "$id" "" "" "" "0" "0" "" "0" "" "" "0" "ttss_setup" "$setup_err" "" "" "$base_dir"
    done
    return 1
  fi

  local setup_json="$base_dir/${id}_ttss_setup/ttss_setup.json"
  [[ -f "$setup_json" ]] || die "missing setup json: $setup_json"
  local requested_count
  for requested_count in $(echo "$BOUNDARY_COUNTS" | tr ',' ' '); do
    run_one_case "$run_no" "$attempt_no" "$requested_count" "$base_dir/cases/shares_${requested_count}" "$setup_json"
  done
  return 0
}

summarize(){
  python3 - "$ATTEMPTS_CSV" "$SUMMARY_TXT" <<'PY'
import csv, statistics, sys, collections
rows = list(csv.DictReader(open(sys.argv[1], newline='')))
by_case = collections.defaultdict(list)
for r in rows:
    by_case[r['case_label']].append(r)
with open(sys.argv[2], 'w', encoding='utf-8') as f:
    f.write(f"total_rows={len(rows)}\n")
    for case in sorted(by_case):
        rs = by_case[case]
        passes = sum(1 for r in rs if r['pass'] == '1')
        rec_ms = [float(r['recover_ms']) for r in rs if r['recover_ms'] not in ('', '0')]
        avg_ms = statistics.mean(rec_ms) if rec_ms else 0.0
        med_ms = statistics.median(rec_ms) if rec_ms else 0.0
        f.write(f"{case}: pass={passes}/{len(rs)}, avg_recover_ms={avg_ms:.2f}, median_recover_ms={med_ms:.2f}\n")
PY
}

check_health
pass_total=0
expected_total=0
run_no=1
while [[ "$run_no" -le "$RUNS" ]]; do
  attempt_no=1
  run_passed=0
  while [[ "$attempt_no" -le $((MAX_RETRIES + 1)) ]]; do
    base_dir="$OUT_DIR/run_$(printf '%03d' "$run_no")/attempt_$(printf '%02d' "$attempt_no")"
    if run_one_attempt "$run_no" "$attempt_no" "$base_dir"; then
      run_passed=1
      break
    fi
    if [[ "$attempt_no" -le "$MAX_RETRIES" ]]; then
      log "run=$run_no attempt=$attempt_no failed at setup, retry after ${RETRY_SLEEP_SEC}s"
      sleep "$RETRY_SLEEP_SEC"
    fi
    attempt_no=$((attempt_no + 1))
  done
  run_no=$((run_no + 1))
done

pass_total="$(python3 - "$ATTEMPTS_CSV" <<'PY'
import csv, sys
rows = list(csv.DictReader(open(sys.argv[1], newline='')))
print(sum(1 for r in rows if r['pass'] == '1'))
PY
)"
expected_total="$(python3 - "$ATTEMPTS_CSV" <<'PY'
import csv, sys
rows = list(csv.DictReader(open(sys.argv[1], newline='')))
print(len(rows))
PY
)"
summarize
log "done: pass_total=$pass_total/$expected_total out=$OUT_DIR"
[[ "$pass_total" -eq "$expected_total" ]] || die "boundary batch failed: pass_total=$pass_total/$expected_total"
log "PASS"
