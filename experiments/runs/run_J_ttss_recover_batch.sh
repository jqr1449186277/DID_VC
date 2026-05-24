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
REGISTER_WAIT_MS="${REGISTER_WAIT_MS:-10000}"
PATH_WAIT_MS="${PATH_WAIT_MS:-10000}"
ROOT_WAIT_MS="${ROOT_WAIT_MS:-10000}"
ROOT_POLL_MS="${ROOT_POLL_MS:-100}"
BB_ASYNC_SUBMIT="${BB_ASYNC_SUBMIT:-0}"
CHECK_OLD_NEW_SHARES="${CHECK_OLD_NEW_SHARES:-1}"
OLD_SHARE_GUARDIAN_INDEX="${OLD_SHARE_GUARDIAN_INDEX:-1}"

TS="$(date +%Y%m%d_%H%M%S)"
OUT_DIR="${OUT_DIR:-$OUT_BASE/${TS}_J_ttss_recover_batch}"
mkdir -p "$OUT_DIR"
RUN_LOG="$OUT_DIR/run.log"
SUMMARY_CSV="$OUT_DIR/summary.csv"
ATTEMPTS_CSV="$OUT_DIR/attempts.csv"
CONFIG_JSON="$OUT_DIR/config.json"

log(){ echo "[run_J_recover_batch] $*" | tee -a "$RUN_LOG"; }
die(){ log "FATAL: $*"; exit 1; }
require_cmd(){ command -v "$1" >/dev/null 2>&1 || die "missing command: $1"; }

require_cmd jq
require_cmd curl
require_cmd python3
[[ -x "$BIN" ]] || die "BIN not executable: $BIN"

now_ms(){ date +%s%3N; }
ms_diff(){ python3 - "$1" "$2" <<'PY'
import sys
print(max(0, int(sys.argv[2]) - int(sys.argv[1])))
PY
}
json_get(){ jq -r "$2 // empty" "$1"; }
json_num(){ jq -r "$2 // 0" "$1"; }

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

jq -nc --arg root "$ROOT" --arg bin "$BIN" --arg bb "$BB" --arg token "$TOKEN" --arg committee_urls "$COMMITTEE_URLS" \
  --argjson runs "$RUNS" --argjson max_retries "$MAX_RETRIES" --argjson retry_sleep_sec "$RETRY_SLEEP_SEC" \
  --argjson ttss_n "$TTSS_N" --argjson ttss_t "$TTSS_T" --arg project_root "$PROJECT_ROOT" \
  '{root:$root,bin:$bin,bb:$bb,token:$token,committee_urls:$committee_urls,runs:$runs,max_retries:$max_retries,retry_sleep_sec:$retry_sleep_sec,ttss_n:$ttss_n,ttss_t:$ttss_t,project_root:$project_root}' > "$CONFIG_JSON"

cat > "$ATTEMPTS_CSV" <<'CSV'
run_no,attempt_no,status,pass,setup_ok,recover_ok,rotate_ok,old_share_invalid_ok,new_share_active_ok,id,id_hash,old_ver,new_ver,old_epoch,new_epoch,ttss_n,ttss_t,setup_ms,register_zk_ms,share_gen_ms,committee_distribute_ms,register_ttss_meta_ms,leaf_fetch_ms,path_fetch_ms,post_meta_verify_ms,post_setup_state_wait_ms,root_wait_ms,setup_total_inner_ms,register_status_poll_ms,ready_probe_path_leaf_ms,ready_wait_sleep_ms,setup_client_pre_register_ms,setup_field_normalize_ms,setup_local_leaf_compute_ms,setup_root_compare_normalize_ms,recover_ms,rotate_ms,apply_recovery_rotate_ms,rotate_response_parse_ms,invalidate_old_shares_ms,new_share_gen_ms,new_share_distribute_ms,set_new_ttss_meta_ms,post_rotate_state_wait_ms,post_rotate_leaf_fetch_ms,post_rotate_path_fetch_ms,old_new_share_validation_ms,post_rotate_checks_ms,new_root_wait_ms,rotate_total_inner_ms,pre_rotate_prepare_ms,rotate_meta_wait_ms,rotate_ready_probe_ms,rotate_client_pre_rotate_ms,rotate_field_normalize_ms,rotate_local_leaf_compute_ms,rotate_root_compare_normalize_ms,recovered_matches_expected,old_recovered_matches,root_changed,new_shares_recover_ok,old_shares_invalidated,share_count_used,tx_root_changed,root_before,root_after,old_leaf,new_leaf,current_leaf,current_leaf_active,current_leaf_matches_old,current_leaf_matches_new,next_setup_json,fail_stage,err,case_dir
CSV

python3 - "$ATTEMPTS_CSV" "$SUMMARY_CSV" <<'PY'
import csv, sys
attempts_csv, summary_csv = sys.argv[1], sys.argv[2]
with open(attempts_csv, newline='') as f:
    attempts_header = next(csv.reader(f))
summary_header = [('final_attempt' if h == 'attempt_no' else 'last_case_dir' if h == 'case_dir' else h) for h in attempts_header]
with open(summary_csv, 'w', newline='') as f:
    csv.writer(f).writerow(summary_header)
PY

append_csv(){
  local csv="$1"; shift
  python3 - "$csv" "$@" <<'PY'
import csv, sys
csv_path = sys.argv[1]
row = sys.argv[2:]
with open(csv_path, 'a', newline='') as f:
    csv.writer(f).writerow(row)
PY
}

fetch_share_for_recover(){
  local committee_url="$1" id_hash="$2" ver="$3" epoch="$4" guardian_index="$5" out_json="$6"
  local payload http_code
  payload="$(jq -nc --arg token "$TOKEN" --arg idHash "$id_hash" --argjson ver "$ver" --argjson epoch "$epoch" --argjson guardianIndex "$guardian_index" --arg nonce "recover_check_${guardian_index}" --arg auditToken "run_J" '{token:$token,idHash:$idHash,ver:$ver,epoch:$epoch,guardianIndex:$guardianIndex,nonce:$nonce,auditToken:$auditToken}')"
  http_code="$(curl -sS -o "$out_json" -w '%{http_code}' -X POST "$committee_url/shareForRecover" -H 'Content-Type: application/json' -d "$payload")" || return 1
  case "$http_code" in
    200|404|409) return 0 ;;
    *) echo '{"ok":0,"err":"http_'"$http_code"'"}' > "$out_json"; return 1 ;;
  esac
}

append_attempt(){
  append_csv "$ATTEMPTS_CSV" \
    "$run_no" "$attempt_no" "$status" "$pass" "$setup_ok" "$recover_ok" "$rotate_ok" "$old_share_invalid_ok" "$new_share_active_ok" \
    "$id" "$id_hash" "$old_ver" "$new_ver" "$old_epoch" "$new_epoch" "$TTSS_N" "$TTSS_T" \
    "$setup_ms" "$register_zk_ms" "$share_gen_ms" "$committee_distribute_ms" "$register_ttss_meta_ms" "$leaf_fetch_ms" "$path_fetch_ms" "$post_meta_verify_ms" "$post_setup_state_wait_ms" "$root_wait_ms" "$setup_total_inner_ms" "$register_status_poll_ms" "$ready_probe_path_leaf_ms" "$ready_wait_sleep_ms" "$setup_client_pre_register_ms" "$setup_field_normalize_ms" "$setup_local_leaf_compute_ms" "$setup_root_compare_normalize_ms" \
    "$recover_ms" "$rotate_ms" "$apply_recovery_rotate_ms" "$rotate_response_parse_ms" "$invalidate_old_shares_ms" "$new_share_gen_ms" "$new_share_distribute_ms" "$set_new_ttss_meta_ms" "$post_rotate_state_wait_ms" "$post_rotate_leaf_fetch_ms" "$post_rotate_path_fetch_ms" "$old_new_share_validation_ms" "$post_rotate_checks_ms" "$new_root_wait_ms" "$rotate_total_inner_ms" "$pre_rotate_prepare_ms" "$rotate_meta_wait_ms" "$rotate_ready_probe_ms" "$rotate_client_pre_rotate_ms" "$rotate_field_normalize_ms" "$rotate_local_leaf_compute_ms" "$rotate_root_compare_normalize_ms" \
    "$recovered_matches_expected" "$old_recovered_matches" "$root_changed" "$new_shares_recover_ok" "$old_shares_invalidated" "$share_count_used" "$tx_root_changed" \
    "$root_before" "$root_after" "$old_leaf" "$new_leaf" "$current_leaf" "$current_leaf_active" "$current_leaf_matches_old" "$current_leaf_matches_new" \
    "$next_setup_json" "$fail_stage" "$err" "$case_dir"
}

run_one_attempt(){
  local run_no="$1" attempt_no="$2" case_dir="$3"
  mkdir -p "$case_dir"
  local status="fail" pass="0" setup_ok="0" recover_ok="0" rotate_ok="0" old_share_invalid_ok="0" new_share_active_ok="0"
  local id="recover_batch_r${run_no}_a${attempt_no}_$(date +%s)" id_hash="" old_ver="" new_ver="" old_epoch="" new_epoch=""
  local setup_ms="0" register_zk_ms="0" share_gen_ms="0" committee_distribute_ms="0" register_ttss_meta_ms="0" leaf_fetch_ms="0" path_fetch_ms="0" post_meta_verify_ms="0" post_setup_state_wait_ms="0" root_wait_ms="0" setup_total_inner_ms="0" register_status_poll_ms="0" ready_probe_path_leaf_ms="0" ready_wait_sleep_ms="0" setup_client_pre_register_ms="0" setup_field_normalize_ms="0" setup_local_leaf_compute_ms="0" setup_root_compare_normalize_ms="0"
  local recover_ms="0" rotate_ms="0" apply_recovery_rotate_ms="0" rotate_response_parse_ms="0" invalidate_old_shares_ms="0" new_share_gen_ms="0" new_share_distribute_ms="0" set_new_ttss_meta_ms="0" post_rotate_state_wait_ms="0" post_rotate_leaf_fetch_ms="0" post_rotate_path_fetch_ms="0" old_new_share_validation_ms="0" post_rotate_checks_ms="0" new_root_wait_ms="0" rotate_total_inner_ms="0" pre_rotate_prepare_ms="0" rotate_meta_wait_ms="0" rotate_ready_probe_ms="0" rotate_client_pre_rotate_ms="0" rotate_field_normalize_ms="0" rotate_local_leaf_compute_ms="0" rotate_root_compare_normalize_ms="0"
  local recovered_matches_expected="0" old_recovered_matches="0" root_changed="0" new_shares_recover_ok="0" old_shares_invalidated="0" share_count_used="0" tx_root_changed="0"
  local root_before="" root_after="" old_leaf="" new_leaf="" current_leaf="" current_leaf_active="0" current_leaf_matches_old="0" current_leaf_matches_new="0" next_setup_json="" fail_stage="" err=""
  IFS=',' read -r -a committee_arr <<< "$COMMITTEE_URLS"
  local first_committee_url="${committee_arr[0]}"
  local start end

  log "run=$run_no attempt=$attempt_no ttss_setup id=$id"
  start="$(now_ms)"
  if ! "$BIN" --ttss_setup --id "$id" --bb "$BB" --committee_urls "$COMMITTEE_URLS" --committee_token "$TOKEN" --ttss_n "$TTSS_N" --ttss_t "$TTSS_T" --project_root "$PROJECT_ROOT" --workdir "$case_dir" --timeout_ms "$TIMEOUT_MS" --register_wait_ms "$REGISTER_WAIT_MS" --path_wait_ms "$PATH_WAIT_MS" --root_wait_ms "$ROOT_WAIT_MS" --root_poll_ms "$ROOT_POLL_MS" --bb_async_submit "$BB_ASYNC_SUBMIT" >"$case_dir/ttss_setup.out" 2>"$case_dir/ttss_setup.err"; then
    fail_stage="ttss_setup"; err="$(tr -d '\n' < "$case_dir/ttss_setup.err" | tail -c 400)"; append_attempt; return 0
  fi
  end="$(now_ms)"; setup_ms="$(ms_diff "$start" "$end")"; setup_ok="1"
  local setup_json="$case_dir/${id}_ttss_setup/ttss_setup.json"
  [[ -f "$setup_json" ]] || { fail_stage="ttss_setup_json"; err="missing $setup_json"; append_attempt; return 0; }
  id_hash="$(json_get "$setup_json" '.idHash')"; old_ver="$(json_get "$setup_json" '.ver')"; old_epoch="$(json_get "$setup_json" '.epoch')"
  register_zk_ms="$(json_num "$setup_json" '.timings.register_zk_ms')"
  share_gen_ms="$(json_num "$setup_json" '.timings.share_gen_ms')"
  committee_distribute_ms="$(json_num "$setup_json" '.timings.committee_distribute_ms')"
  register_ttss_meta_ms="$(json_num "$setup_json" '.timings.register_ttss_meta_ms')"
  leaf_fetch_ms="$(json_num "$setup_json" '.timings.leaf_fetch_ms')"
  path_fetch_ms="$(json_num "$setup_json" '.timings.path_fetch_ms')"
  post_meta_verify_ms="$(json_num "$setup_json" '.timings.post_meta_verify_ms')"
  post_setup_state_wait_ms="$(json_num "$setup_json" '.timings.post_setup_state_wait_ms')"
  root_wait_ms="$(json_num "$setup_json" '.timings.root_wait_ms')"
  setup_total_inner_ms="$(json_num "$setup_json" '.timings.setup_total_inner_ms')"
  register_status_poll_ms="$(json_num "$setup_json" '.timings.register_status_poll_ms')"
  ready_probe_path_leaf_ms="$(json_num "$setup_json" '.timings.ready_probe_path_leaf_ms')"
  ready_wait_sleep_ms="$(json_num "$setup_json" '.timings.ready_wait_sleep_ms')"
  setup_client_pre_register_ms="$(json_num "$setup_json" '.timings.client_pre_register_ms')"
  setup_field_normalize_ms="$(json_num "$setup_json" '.timings.field_normalize_ms')"
  setup_local_leaf_compute_ms="$(json_num "$setup_json" '.timings.local_leaf_compute_ms')"
  setup_root_compare_normalize_ms="$(json_num "$setup_json" '.timings.root_compare_normalize_ms')"

  log "run=$run_no attempt=$attempt_no ttss_recover"
  start="$(now_ms)"
  if ! "$BIN" --ttss_recover --id "$id" --bb "$BB" --committee_urls "$COMMITTEE_URLS" --committee_token "$TOKEN" --ttss_n "$TTSS_N" --ttss_t "$TTSS_T" --ttss_state "$setup_json" --project_root "$PROJECT_ROOT" --workdir "$case_dir" --timeout_ms "$TIMEOUT_MS" --register_wait_ms "$REGISTER_WAIT_MS" --path_wait_ms "$PATH_WAIT_MS" --root_wait_ms "$ROOT_WAIT_MS" --root_poll_ms "$ROOT_POLL_MS" --bb_async_submit "$BB_ASYNC_SUBMIT" >"$case_dir/ttss_recover.out" 2>"$case_dir/ttss_recover.err"; then
    fail_stage="ttss_recover"; err="$(tr -d '\n' < "$case_dir/ttss_recover.err" | tail -c 400)"; append_attempt; return 0
  fi
  end="$(now_ms)"; recover_ms="$(ms_diff "$start" "$end")"
  local recover_json="$case_dir/${id}_ttss_recover/recover_result.json"
  [[ -f "$recover_json" ]] || { fail_stage="recover_json"; err="missing $recover_json"; append_attempt; return 0; }
  recovered_matches_expected="$(json_get "$recover_json" '.recoveredMatchesExpected')"
  share_count_used="$(json_num "$recover_json" '.shareCountUsed')"
  [[ "$recovered_matches_expected" == "true" || "$recovered_matches_expected" == "1" ]] || { fail_stage="recover_mismatch"; err="recover_result not matched expected"; append_attempt; return 0; }
  recover_ok="1"

  log "run=$run_no attempt=$attempt_no ttss_recover_and_rotate"
  start="$(now_ms)"
  if ! "$BIN" --ttss_recover_and_rotate --id "$id" --bb "$BB" --committee_urls "$COMMITTEE_URLS" --committee_token "$TOKEN" --ttss_n "$TTSS_N" --ttss_t "$TTSS_T" --ttss_state "$setup_json" --project_root "$PROJECT_ROOT" --workdir "$case_dir" --timeout_ms "$TIMEOUT_MS" --register_wait_ms "$REGISTER_WAIT_MS" --path_wait_ms "$PATH_WAIT_MS" --root_wait_ms "$ROOT_WAIT_MS" --root_poll_ms "$ROOT_POLL_MS" --bb_async_submit "$BB_ASYNC_SUBMIT" >"$case_dir/ttss_recover_and_rotate.out" 2>"$case_dir/ttss_recover_and_rotate.err"; then
    fail_stage="ttss_recover_and_rotate"; err="$(tr -d '\n' < "$case_dir/ttss_recover_and_rotate.err" | tail -c 400)"; append_attempt; return 0
  fi
  end="$(now_ms)"; rotate_ms="$(ms_diff "$start" "$end")"
  local rotate_json="$case_dir/${id}_ttss_recover_and_rotate/ttss_rotate_result.json"
  [[ -f "$rotate_json" ]] || { fail_stage="rotate_json"; err="missing $rotate_json"; append_attempt; return 0; }
  old_recovered_matches="$(json_get "$rotate_json" '.oldRecoveredMatches')"
  root_changed="$(json_get "$rotate_json" '.rootChanged')"
  new_shares_recover_ok="$(json_get "$rotate_json" '.newSharesRecoverOk')"
  old_shares_invalidated="$(json_get "$rotate_json" '.oldSharesInvalidated')"
  next_setup_json="$(json_get "$rotate_json" '.nextSetupJson')"
  new_ver="$(json_get "$rotate_json" '.newVer')"; new_epoch="$(json_get "$rotate_json" '.newEpoch')"
  apply_recovery_rotate_ms="$(json_num "$rotate_json" '.timings.apply_recovery_rotate_ms')"
  rotate_response_parse_ms="$(json_num "$rotate_json" '.timings.rotate_response_parse_ms')"
  invalidate_old_shares_ms="$(json_num "$rotate_json" '.timings.invalidate_old_shares_ms')"
  new_share_gen_ms="$(json_num "$rotate_json" '.timings.new_share_gen_ms')"
  new_share_distribute_ms="$(json_num "$rotate_json" '.timings.new_share_distribute_ms')"
  set_new_ttss_meta_ms="$(json_num "$rotate_json" '.timings.set_new_ttss_meta_ms')"
  post_rotate_state_wait_ms="$(json_num "$rotate_json" '.timings.post_rotate_state_wait_ms')"
  post_rotate_leaf_fetch_ms="$(json_num "$rotate_json" '.timings.post_rotate_leaf_fetch_ms')"
  post_rotate_path_fetch_ms="$(json_num "$rotate_json" '.timings.post_rotate_path_fetch_ms')"
  old_new_share_validation_ms="$(json_num "$rotate_json" '.timings.old_new_share_validation_ms')"
  post_rotate_checks_ms="$(json_num "$rotate_json" '.timings.post_rotate_checks_ms')"
  new_root_wait_ms="$(json_num "$rotate_json" '.timings.new_root_wait_ms')"
  rotate_total_inner_ms="$(json_num "$rotate_json" '.timings.rotate_total_inner_ms')"
  pre_rotate_prepare_ms="$(json_num "$rotate_json" '.timings.pre_rotate_prepare_ms')"
  rotate_meta_wait_ms="$(json_num "$rotate_json" '.timings.rotate_meta_wait_ms')"
  rotate_ready_probe_ms="$(json_num "$rotate_json" '.timings.rotate_ready_probe_ms')"
  rotate_client_pre_rotate_ms="$(json_num "$rotate_json" '.timings.client_pre_rotate_ms')"
  rotate_field_normalize_ms="$(json_num "$rotate_json" '.timings.field_normalize_ms')"
  rotate_local_leaf_compute_ms="$(json_num "$rotate_json" '.timings.local_leaf_compute_ms')"
  rotate_root_compare_normalize_ms="$(json_num "$rotate_json" '.timings.root_compare_normalize_ms')"
  root_before="$(json_get "$rotate_json" '.rootBefore')"; root_after="$(json_get "$rotate_json" '.rootAfter')"
  old_leaf="$(json_get "$rotate_json" '.oldLeaf')"; new_leaf="$(json_get "$rotate_json" '.newLeaf')"; current_leaf="$(json_get "$rotate_json" '.currentLeaf')"
  current_leaf_active="$(json_get "$rotate_json" '.currentLeafActive')"; current_leaf_matches_old="$(json_get "$rotate_json" '.currentLeafMatchesOld')"; current_leaf_matches_new="$(json_get "$rotate_json" '.currentLeafMatchesNew')"
  [[ "$old_recovered_matches" == "true" || "$old_recovered_matches" == "1" ]] || { fail_stage="rotate_old_recover_mismatch"; err="oldRecoveredMatches=false"; append_attempt; return 0; }
  [[ "$root_changed" == "true" || "$root_changed" == "1" ]] || { fail_stage="rotate_root_unchanged"; err="rootChanged=false"; append_attempt; return 0; }
  [[ "$new_shares_recover_ok" == "true" || "$new_shares_recover_ok" == "1" ]] || { fail_stage="rotate_new_shares_recover"; err="newSharesRecoverOk=false"; append_attempt; return 0; }
  [[ "$old_shares_invalidated" == "true" || "$old_shares_invalidated" == "1" ]] || { fail_stage="rotate_old_shares_invalidated"; err="oldSharesInvalidated=false"; append_attempt; return 0; }
  rotate_ok="1"; tx_root_changed="1"

  if [[ "$CHECK_OLD_NEW_SHARES" == "1" ]]; then
    local old_check_json="$case_dir/old_share_check.json" new_check_json="$case_dir/new_share_check.json"
    fetch_share_for_recover "$first_committee_url" "$id_hash" "$old_ver" "$old_epoch" "$OLD_SHARE_GUARDIAN_INDEX" "$old_check_json" || { fail_stage="old_share_check_call"; err="old share check request failed"; append_attempt; return 0; }
    fetch_share_for_recover "$first_committee_url" "$id_hash" "$new_ver" "$new_epoch" "$OLD_SHARE_GUARDIAN_INDEX" "$new_check_json" || { fail_stage="new_share_check_call"; err="new share check request failed"; append_attempt; return 0; }
    local old_ok old_err new_ok new_active
    old_ok="$(json_get "$old_check_json" '.ok')"; old_err="$(json_get "$old_check_json" '.err')"; new_ok="$(json_get "$new_check_json" '.ok')"; new_active="$(json_get "$new_check_json" '.shareEnvelope.active')"
    if [[ "$old_ok" == "0" && ( "$old_err" == "share_inactive" || "$old_err" == "no_active_share" ) ]]; then old_share_invalid_ok="1"; else fail_stage="old_share_check"; err="old share still active"; append_attempt; return 0; fi
    if [[ "$new_ok" == "1" && ( "$new_active" == "true" || "$new_active" == "1" ) ]]; then new_share_active_ok="1"; else fail_stage="new_share_check"; err="new share not active"; append_attempt; return 0; fi
  else
    old_share_invalid_ok="1"; new_share_active_ok="1"
  fi

  status="pass"; pass="1"; append_attempt
}

check_health
pass_count=0
retry_count_total=0
run_no=1
while [[ "$run_no" -le "$RUNS" ]]; do
  attempt_no=1
  final_attempt=0
  while [[ "$attempt_no" -le $((MAX_RETRIES + 1)) ]]; do
    case_dir="$OUT_DIR/run_$(printf '%03d' "$run_no")/attempt_$(printf '%02d' "$attempt_no")"
    mkdir -p "$case_dir"
    run_one_attempt "$run_no" "$attempt_no" "$case_dir"
    final_attempt="$attempt_no"
    last_status="$(python3 - "$ATTEMPTS_CSV" <<'PY'
import csv, sys
with open(sys.argv[1], newline='') as f:
    rows = list(csv.DictReader(f))
print(rows[-1]['status'] if rows else '')
PY
)"
    if [[ "$last_status" == "pass" ]]; then pass_count=$((pass_count + 1)); break; fi
    if [[ "$attempt_no" -le "$MAX_RETRIES" ]]; then retry_count_total=$((retry_count_total + 1)); log "run=$run_no attempt=$attempt_no failed, retry after ${RETRY_SLEEP_SEC}s"; sleep "$RETRY_SLEEP_SEC"; fi
    attempt_no=$((attempt_no + 1))
  done
  python3 - "$ATTEMPTS_CSV" "$SUMMARY_CSV" "$run_no" <<'PY'
import csv, sys
attempts_csv, summary_csv, run_no = sys.argv[1], sys.argv[2], sys.argv[3]
with open(attempts_csv, newline='') as f:
    reader = csv.DictReader(f)
    attempts_header = reader.fieldnames[:]
    rows = [r for r in reader if r.get("run_no") == run_no]
if rows:
    row = rows[-1].copy()
    summary_header = []
    mapped = {}
    for h in attempts_header:
        if h == "attempt_no":
            summary_header.append("final_attempt")
            mapped["final_attempt"] = row.get(h, "")
        elif h == "case_dir":
            summary_header.append("last_case_dir")
            mapped["last_case_dir"] = row.get(h, "")
        else:
            summary_header.append(h)
            mapped[h] = row.get(h, "")
    with open(summary_csv, 'a', newline='') as f:
        csv.DictWriter(f, fieldnames=summary_header).writerow(mapped)
PY
  run_no=$((run_no + 1))
done

log "done: pass_count=$pass_count/$RUNS, retry_count_total=$retry_count_total, out=$OUT_DIR"
[[ "$pass_count" -eq "$RUNS" ]] || die "batch failed: pass_count=$pass_count/$RUNS"
log "PASS"
