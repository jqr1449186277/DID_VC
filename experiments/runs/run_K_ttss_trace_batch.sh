#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

ROOT="${ROOT:-${PROJECT_ROOT:-$(cd "$SCRIPT_DIR/../.." && pwd)}}"
BIN="${BIN:-$ROOT/build/did_demo_zk}"
TRACER="${TRACER:-$ROOT/hardhat/tracer_client.js}"
OUT_BASE="${OUT_BASE:-$ROOT/results}"
BB="${BB:-http://127.0.0.1:3000}"
PIRATE="${PIRATE:-http://127.0.0.1:4000}"
TOKEN="${TOKEN:-demo-token}"
COMMITTEE_URLS="${COMMITTEE_URLS:-http://127.0.0.1:8001,http://127.0.0.1:8002,http://127.0.0.1:8003,http://127.0.0.1:8004,http://127.0.0.1:8005,http://127.0.0.1:8006}"
RUNS="${RUNS:-10}"
MAX_RETRIES="${MAX_RETRIES:-2}"
RETRY_SLEEP_SEC="${RETRY_SLEEP_SEC:-2}"
TTSS_N="${TTSS_N:-6}"
TTSS_T="${TTSS_T:-4}"
DELTA="${DELTA:-0.000001}"
CHALLENGE_COUNT="${CHALLENGE_COUNT:-1}"
MAX_QUERIES="${MAX_QUERIES:-3}"
LEAKED_INDEXES="${LEAKED_INDEXES:-2,4}"
STRICT_FULL_SET="${STRICT_FULL_SET:-1}"
ENABLE_MAIN_CROSSCHECK="${ENABLE_MAIN_CROSSCHECK:-0}"
SKIP_PUBLISH="${SKIP_PUBLISH:-0}"
PROJECT_ROOT="${PROJECT_ROOT:-$ROOT}"
TIMEOUT_MS="${TIMEOUT_MS:-120000}"
REGISTER_WAIT_MS="${REGISTER_WAIT_MS:-5000}"
PATH_WAIT_MS="${PATH_WAIT_MS:-5000}"
ROOT_WAIT_MS="${ROOT_WAIT_MS:-15000}"
ROOT_POLL_MS="${ROOT_POLL_MS:-500}"

TS="$(date +%Y%m%d_%H%M%S)"
OUT_DIR="${OUT_DIR:-$OUT_BASE/${TS}_K_ttss_trace_batch}"
mkdir -p "$OUT_DIR"

RUN_LOG="$OUT_DIR/run.log"
SUMMARY_CSV="$OUT_DIR/summary.csv"
ATTEMPTS_CSV="$OUT_DIR/attempts.csv"
CONFIG_JSON="$OUT_DIR/config.json"

log(){ echo "[run_K_trace_batch] $*" | tee -a "$RUN_LOG"; }
die(){ log "FATAL: $*"; exit 1; }
require_cmd(){ command -v "$1" >/dev/null 2>&1 || die "missing command: $1"; }

require_cmd jq
require_cmd curl
require_cmd node
require_cmd python3
[[ -x "$BIN" ]] || die "BIN not executable: $BIN"
[[ -f "$TRACER" ]] || die "tracer_client.js not found: $TRACER"

now_ms(){ date +%s%3N; }
ms_diff(){ python3 - "$1" "$2" <<'PY'
import sys
print(max(0, int(sys.argv[2]) - int(sys.argv[1])))
PY
}
json_get(){ jq -r "$2 // empty" "$1"; }
json_num(){ jq -r "$2 // 0" "$1"; }

cleanup_box(){
  local box_id="${1:-}"
  if [[ -n "$box_id" ]]; then
    curl -fsS -X POST "$PIRATE/clear" \
      -H 'Content-Type: application/json' \
      -d "$(jq -nc --arg token "$TOKEN" --arg boxId "$box_id" '{token:$token, boxId:$boxId}')" \
      >/dev/null 2>&1 || true
  fi
}

check_health(){
  log "health check: bb"
  curl -fsS "$BB/health" >/dev/null || die "bb health failed: $BB/health"
  log "health check: pirate"
  curl -fsS "$PIRATE/health" >/dev/null || die "pirate health failed: $PIRATE/health"
  log "health check: committee"
  IFS=',' read -r -a committee_arr <<< "$COMMITTEE_URLS"
  local u
  for u in "${committee_arr[@]}"; do
    curl -fsS "$u/health" >/dev/null || die "committee health failed: $u/health"
  done
}

build_sets(){
  python3 - "$COMMITTEE_URLS" "$LEAKED_INDEXES" <<'PY'
import sys, json
urls = [u.strip() for u in sys.argv[1].split(',') if u.strip()]
leaked = [int(x) for x in sys.argv[2].split(',') if x.strip()]
honest_urls = []
for idx, u in enumerate(urls, start=1):
    if idx not in leaked:
        honest_urls.append(u)
print(json.dumps({"all_urls": urls, "leaked": leaked, "honest_urls": honest_urls}, ensure_ascii=False))
PY
}

SETS_JSON="$(build_sets)"
HONEST_URLS="$(printf '%s' "$SETS_JSON" | jq -r '.honest_urls | join(",")')"
LEAKED_COUNT="$(printf '%s' "$SETS_JSON" | jq -r '.leaked | length')"
HONEST_COUNT="$(printf '%s' "$SETS_JSON" | jq -r '.honest_urls | length')"
[[ "$LEAKED_COUNT" -lt "$TTSS_T" ]] || die "LEAKED_INDEXES must have size < TTSS_T"
[[ "$HONEST_COUNT" -ge 1 ]] || die "no honest committee urls left after excluding leaked indexes"

jq -nc \
  --arg root "$ROOT" \
  --arg bin "$BIN" \
  --arg tracer "$TRACER" \
  --arg bb "$BB" \
  --arg pirate "$PIRATE" \
  --arg token "$TOKEN" \
  --arg committee_urls "$COMMITTEE_URLS" \
  --arg honest_urls "$HONEST_URLS" \
  --arg leaked_indexes "$LEAKED_INDEXES" \
  --argjson runs "$RUNS" \
  --argjson max_retries "$MAX_RETRIES" \
  --argjson retry_sleep_sec "$RETRY_SLEEP_SEC" \
  --argjson ttss_n "$TTSS_N" \
  --argjson ttss_t "$TTSS_T" \
  --arg delta "$DELTA" \
  --argjson challenge_count "$CHALLENGE_COUNT" \
  --argjson max_queries "$MAX_QUERIES" \
  --argjson strict_full_set "$STRICT_FULL_SET" \
  --argjson enable_main_crosscheck "$ENABLE_MAIN_CROSSCHECK" \
  --argjson skip_publish "$SKIP_PUBLISH" \
  --arg project_root "$PROJECT_ROOT" \
  --argjson timeout_ms "$TIMEOUT_MS" \
  --argjson register_wait_ms "$REGISTER_WAIT_MS" \
  --argjson path_wait_ms "$PATH_WAIT_MS" \
  --argjson root_wait_ms "$ROOT_WAIT_MS" \
  --argjson root_poll_ms "$ROOT_POLL_MS" \
  '{root:$root,bin:$bin,tracer:$tracer,bb:$bb,pirate:$pirate,token:$token,committee_urls:$committee_urls,honest_urls:$honest_urls,leaked_indexes:$leaked_indexes,runs:$runs,max_retries:$max_retries,retry_sleep_sec:$retry_sleep_sec,ttss_n:$ttss_n,ttss_t:$ttss_t,delta:$delta,challenge_count:$challenge_count,max_queries:$max_queries,strict_full_set:$strict_full_set,enable_main_crosscheck:$enable_main_crosscheck,skip_publish:$skip_publish,project_root:$project_root,timeout_ms:$timeout_ms,register_wait_ms:$register_wait_ms,path_wait_ms:$path_wait_ms,root_wait_ms:$root_wait_ms,root_poll_ms:$root_poll_ms}' \
  > "$CONFIG_JSON"

cat > "$SUMMARY_CSV" <<'CSV'
run_no,final_attempt,status,pass,setup_ok,box_load_ok,oracle_probe_ok,trace_run_ok,trace_verify_ok,publish_ok,main_crosscheck_ok,id,id_hash,ver,epoch,ttss_n,ttss_t,delta,leaked_indexes,honest_count,challenge_count,max_queries,setup_ms,load_box_ms,oracle_probe_ms,trace_run_ms,trace_verify_ms,publish_ms,trace_query_count,accused_set_size,accused_set,expected_leaked_set,hit_expected,exact_match,verify_accepted,tx_hash,trace_digest,box_id,trace_session_id,oracle_result_type,oracle_ok,fail_stage,err,last_case_dir
CSV

cat > "$ATTEMPTS_CSV" <<'CSV'
run_no,attempt_no,status,pass,setup_ok,box_load_ok,oracle_probe_ok,trace_run_ok,trace_verify_ok,publish_ok,main_crosscheck_ok,id,id_hash,ver,epoch,ttss_n,ttss_t,delta,leaked_indexes,honest_count,challenge_count,max_queries,setup_ms,load_box_ms,oracle_probe_ms,trace_run_ms,trace_verify_ms,publish_ms,trace_query_count,accused_set_size,accused_set,expected_leaked_set,hit_expected,exact_match,verify_accepted,tx_hash,trace_digest,box_id,trace_session_id,oracle_result_type,oracle_ok,fail_stage,err,case_dir
CSV

append_csv(){
  local csv="$1"
  shift
  python3 - "$csv" "$@" <<'PY'
import csv, sys
csv_path = sys.argv[1]
row = sys.argv[2:]
with open(csv_path, 'a', newline='') as f:
    csv.writer(f).writerow(row)
PY
}

fetch_share_for_trace(){
  local committee_url="$1"
  local trace_session_id="$2"
  local id_hash="$3"
  local ver="$4"
  local epoch="$5"
  local out_json="$6"
  local payload
  payload="$(jq -nc \
    --arg token "$TOKEN" \
    --arg traceSessionId "$trace_session_id" \
    --arg idHash "$id_hash" \
    --argjson ver "$ver" \
    --argjson epoch "$epoch" \
    '{token:$token, traceSessionId:$traceSessionId, idHash:$idHash, ver:$ver, epoch:$epoch, requestKind:"honest_challenge"}')"
  curl -fsS -X POST "$committee_url/shareForTrace" \
    -H 'Content-Type: application/json' \
    -d "$payload" | tee "$out_json" >/dev/null
}

load_static_box(){
  local id_hash="$1"
  local ver="$2"
  local epoch="$3"
  local leaked_array_json="$4"
  local out_json="$5"
  local payload
  payload="$(jq -nc \
    --arg token "$TOKEN" \
    --arg idHash "$id_hash" \
    --argjson ver "$ver" \
    --argjson epoch "$epoch" \
    --argjson n "$TTSS_N" \
    --argjson t "$TTSS_T" \
    --argjson f "$LEAKED_COUNT" \
    --argjson leakedShares "$leaked_array_json" \
    '{token:$token, boxConfig:{scheme:"NITS-Shamir-v1", mode:"stateless", idHash:$idHash, ver:$ver, epoch:$epoch, n:$n, t:$t, f:$f, secretType:"srec_seed", leakedShares:$leakedShares, outputMode:"seed"}}')"
  curl -fsS -X POST "$PIRATE/loadStaticBox" \
    -H 'Content-Type: application/json' \
    -d "$payload" | tee "$out_json" >/dev/null
}

oracle_probe(){
  local box_id="$1"
  local id_hash="$2"
  local ver="$3"
  local epoch="$4"
  local honest_shares_json="$5"
  local out_json="$6"
  local payload
  payload="$(jq -nc \
    --arg boxId "$box_id" \
    --arg queryId "probe_q_0001" \
    --arg idHash "$id_hash" \
    --argjson ver "$ver" \
    --argjson epoch "$epoch" \
    --argjson honestShares "$honest_shares_json" \
    '{boxId:$boxId, queryId:$queryId, idHash:$idHash, ver:$ver, epoch:$epoch, honestShares:$honestShares}')"
  curl -fsS -X POST "$PIRATE/oracle" \
    -H 'Content-Type: application/json' \
    -d "$payload" | tee "$out_json" >/dev/null
}

append_attempt(){
  append_csv "$ATTEMPTS_CSV" \
    "$run_no" "$attempt_no" "$status" "$pass" "$setup_ok" "$box_load_ok" "$oracle_probe_ok" "$trace_run_ok" "$trace_verify_ok" "$publish_ok" "$main_crosscheck_ok" \
    "$id" "$id_hash" "$ver" "$epoch" "$TTSS_N" "$TTSS_T" "$DELTA" "$LEAKED_INDEXES" "$HONEST_COUNT" "$CHALLENGE_COUNT" "$MAX_QUERIES" \
    "$setup_ms" "$load_box_ms" "$oracle_probe_ms" "$trace_run_ms" "$trace_verify_ms" "$publish_ms" "$trace_query_count" "$accused_set_size" "$accused_set" "$LEAKED_INDEXES" "$hit_expected" "$exact_match" "$verify_accepted" "$tx_hash" "$trace_digest" "$box_id" "$trace_session_id" "$oracle_result_type" "$oracle_ok" "$fail_stage" "$err" "$case_dir"
}

run_one_attempt(){
  local run_no="$1"
  local attempt_no="$2"
  local case_dir="$3"

  mkdir -p "$case_dir" "$case_dir/ttss_setup" "$case_dir/leaked_shares" "$case_dir/honest_probe"

  local status="fail"
  local pass="0"
  local setup_ok="0"
  local box_load_ok="0"
  local oracle_probe_ok="0"
  local trace_run_ok="0"
  local trace_verify_ok="0"
  local publish_ok="0"
  local main_crosscheck_ok="0"
  local fail_stage=""
  local err=""
  local id="trace_batch_r${run_no}_a${attempt_no}_$(date +%s)"
  local id_hash=""
  local ver=""
  local epoch=""
  local setup_ms="0"
  local load_box_ms="0"
  local oracle_probe_ms="0"
  local trace_run_ms="0"
  local trace_verify_ms="0"
  local publish_ms="0"
  local box_id=""
  local trace_session_id="trace_${TS}_r${run_no}_a${attempt_no}"
  local oracle_result_type=""
  local oracle_ok="0"
  local trace_query_count="0"
  local accused_set_size="0"
  local accused_set=""
  local hit_expected="0"
  local exact_match="0"
  local verify_accepted="0"
  local tx_hash=""
  local trace_digest=""

  trap 'cleanup_box "$box_id"' RETURN

  log "run=$run_no attempt=$attempt_no ttss_setup id=$id"
  local start end
  start="$(now_ms)"
  if ! "$BIN" \
      --ttss_setup \
      --id "$id" \
      --bb "$BB" \
      --committee_urls "$COMMITTEE_URLS" \
      --committee_token "$TOKEN" \
      --ttss_n "$TTSS_N" \
      --ttss_t "$TTSS_T" \
      --project_root "$PROJECT_ROOT" \
      --workdir "$case_dir" \
      --timeout_ms "$TIMEOUT_MS" \
      --register_wait_ms "$REGISTER_WAIT_MS" \
      --path_wait_ms "$PATH_WAIT_MS" \
      --root_wait_ms "$ROOT_WAIT_MS" \
      --root_poll_ms "$ROOT_POLL_MS" \
      >"$case_dir/ttss_setup.out" 2>"$case_dir/ttss_setup.err"; then
    fail_stage="ttss_setup"
    err="$(tr -d '\n' < "$case_dir/ttss_setup.err" | tail -c 400)"
    append_attempt
    return 0
  fi
  end="$(now_ms)"
  setup_ms="$(ms_diff "$start" "$end")"
  setup_ok="1"

  local setup_json="$case_dir/${id}_ttss_setup/ttss_setup.json"
  local vk_json="$case_dir/${id}_ttss_setup/ttss_vk.json"
  [[ -f "$setup_json" ]] || { fail_stage="ttss_setup_json"; err="missing $setup_json"; append_attempt; return 0; }
  [[ -f "$vk_json" ]] || { fail_stage="ttss_vk_json"; err="missing $vk_json"; append_attempt; return 0; }

  id_hash="$(json_get "$setup_json" '.idHash')"
  ver="$(json_get "$setup_json" '.ver')"
  epoch="$(json_get "$setup_json" '.epoch')"
  [[ -n "$id_hash" && -n "$ver" && -n "$epoch" ]] || { fail_stage="ttss_setup_tuple"; err="missing idHash/ver/epoch in setup"; append_attempt; return 0; }

  local leaked_files=()
  local honest_probe_files=()
  local leaked_array_json honest_probe_array_json
  IFS=',' read -r -a committee_arr <<< "$COMMITTEE_URLS"
  IFS=',' read -r -a leaked_arr <<< "$LEAKED_INDEXES"
  local idx committee_url reply_file
  for idx in "${leaked_arr[@]}"; do
    committee_url="${committee_arr[$((idx-1))]}"
    reply_file="$case_dir/leaked_shares/guardian_${idx}.json"
    if ! fetch_share_for_trace "$committee_url" "$trace_session_id" "$id_hash" "$ver" "$epoch" "$reply_file"; then
      fail_stage="fetch_leaked_share_g${idx}"
      err="shareForTrace failed at guardian ${idx}"
      append_attempt
      return 0
    fi
    leaked_files+=("$reply_file")
  done
  leaked_array_json="$(printf '%s\n' "${leaked_files[@]}" | python3 -c 'import json,sys; files=[line.strip() for line in sys.stdin if line.strip()]; arr=[]
for p in files:
    obj=json.load(open(p))
    arr.append(obj.get("shareEnvelope", obj))
print(json.dumps(arr, ensure_ascii=False))')"

  local honest_need=$(( TTSS_T - LEAKED_COUNT ))
  local honest_urls_arr=()
  IFS=',' read -r -a honest_urls_arr <<< "$HONEST_URLS"
  local i
  for ((i=0; i<honest_need && i<${#honest_urls_arr[@]}; i++)); do
    reply_file="$case_dir/honest_probe/honest_$((i+1)).json"
    if ! fetch_share_for_trace "${honest_urls_arr[$i]}" "${trace_session_id}_probe" "$id_hash" "$ver" "$epoch" "$reply_file"; then
      fail_stage="fetch_honest_probe_$((i+1))"
      err="shareForTrace probe failed"
      append_attempt
      return 0
    fi
    honest_probe_files+=("$reply_file")
  done
  honest_probe_array_json="$(printf '%s\n' "${honest_probe_files[@]}" | python3 -c 'import json,sys; files=[line.strip() for line in sys.stdin if line.strip()]; arr=[]
for p in files:
    obj=json.load(open(p))
    arr.append(obj.get("shareEnvelope", obj))
print(json.dumps(arr, ensure_ascii=False))')"

  log "run=$run_no attempt=$attempt_no loadStaticBox"
  start="$(now_ms)"
  if ! load_static_box "$id_hash" "$ver" "$epoch" "$leaked_array_json" "$case_dir/box_load.json"; then
    fail_stage="load_static_box"
    err="$(tr -d '\n' < "$case_dir/box_load.json" | tail -c 400)"
    append_attempt
    return 0
  fi
  end="$(now_ms)"
  load_box_ms="$(ms_diff "$start" "$end")"
  box_id="$(json_get "$case_dir/box_load.json" '.boxId')"
  if [[ "$(json_get "$case_dir/box_load.json" '.ok')" != "1" || -z "$box_id" ]]; then
    fail_stage="load_static_box"
    err="$(tr -d '\n' < "$case_dir/box_load.json" | tail -c 400)"
    append_attempt
    return 0
  fi
  box_load_ok="1"

  log "run=$run_no attempt=$attempt_no oracle probe"
  start="$(now_ms)"
  if ! oracle_probe "$box_id" "$id_hash" "$ver" "$epoch" "$honest_probe_array_json" "$case_dir/oracle_probe.json"; then
    fail_stage="oracle_probe"
    err="oracle probe failed"
    append_attempt
    return 0
  fi
  end="$(now_ms)"
  oracle_probe_ms="$(ms_diff "$start" "$end")"
  oracle_ok="$(json_get "$case_dir/oracle_probe.json" '.ok')"
  oracle_result_type="$(json_get "$case_dir/oracle_probe.json" '.resultType')"
  if [[ "$oracle_ok" != "1" ]]; then
    fail_stage="oracle_probe"
    err="$(tr -d '\n' < "$case_dir/oracle_probe.json" | tail -c 400)"
    append_attempt
    return 0
  fi
  oracle_probe_ok="1"

  log "run=$run_no attempt=$attempt_no tracer_client trace-run"
  start="$(now_ms)"
  if ! node "$TRACER" trace-run \
      --idHash "$id_hash" \
      --ver "$ver" \
      --epoch "$epoch" \
      --n "$TTSS_N" \
      --t "$TTSS_T" \
      --delta "$DELTA" \
      --vk-file "$vk_json" \
      --committee "$HONEST_URLS" \
      --pirate "$PIRATE" \
      --box-id "$box_id" \
      --challenge-count "$CHALLENGE_COUNT" \
      --max-queries "$MAX_QUERIES" \
      --out "$case_dir/trace_result.json" \
      >"$case_dir/trace_run.out" 2>"$case_dir/trace_run.err"; then
    fail_stage="trace_run"
    err="$(tr -d '\n' < "$case_dir/trace_run.err" | tail -c 400)"
    append_attempt
    return 0
  fi
  end="$(now_ms)"
  trace_run_ms="$(ms_diff "$start" "$end")"
  [[ -f "$case_dir/trace_result.json" ]] || { fail_stage="trace_run_result"; err="missing trace_result.json"; append_attempt; return 0; }
  trace_run_ok="1"

  if [[ -f "$case_dir/trace_queries.json" ]]; then
    trace_query_count="$(json_num "$case_dir/trace_queries.json" '.queryCount')"
  else
    trace_query_count="$(json_num "$case_dir/trace_result.json" '.traceMeta.queryCount')"
  fi
  accused_set_size="$(json_num "$case_dir/trace_result.json" '.accusedSet | length')"
  accused_set="$(jq -c '.accusedSet // []' "$case_dir/trace_result.json")"

  python3 - "$LEAKED_INDEXES" "$accused_set" "$STRICT_FULL_SET" >"$case_dir/set_check.json" <<'PY'
import json, sys
exp = [int(x) for x in sys.argv[1].split(',') if x.strip()]
acc = json.loads(sys.argv[2])
strict = int(sys.argv[3])
exp_set = set(exp)
acc_set = set(int(x) for x in acc)
out = {
  "hit_expected": int(len(exp_set & acc_set) > 0),
  "exact_match": int(exp_set == acc_set),
  "pass_set_check": int((exp_set == acc_set) if strict else (len(exp_set & acc_set) > 0))
}
print(json.dumps(out))
PY
  hit_expected="$(json_get "$case_dir/set_check.json" '.hit_expected')"
  exact_match="$(json_get "$case_dir/set_check.json" '.exact_match')"

  log "run=$run_no attempt=$attempt_no tracer_client trace-verify"
  start="$(now_ms)"
  if ! node "$TRACER" trace-verify \
      --vk-file "$vk_json" \
      --trace-result "$case_dir/trace_result.json" \
      --out "$case_dir/trace_verify.json" \
      >"$case_dir/trace_verify.out" 2>"$case_dir/trace_verify.err"; then
    fail_stage="trace_verify"
    err="$(tr -d '\n' < "$case_dir/trace_verify.err" | tail -c 400)"
    append_attempt
    return 0
  fi
  end="$(now_ms)"
  trace_verify_ms="$(ms_diff "$start" "$end")"
  verify_accepted="$(json_get "$case_dir/trace_verify.json" '.accepted')"
  if [[ "$verify_accepted" != "true" && "$verify_accepted" != "1" ]]; then
    fail_stage="trace_verify_reject"
    err="trace verify not accepted"
    append_attempt
    return 0
  fi
  trace_verify_ok="1"

  if [[ "$SKIP_PUBLISH" == "0" ]]; then
    log "run=$run_no attempt=$attempt_no tracer_client publish-trace"
    start="$(now_ms)"
    if ! node "$TRACER" publish-trace \
        --bb "$BB" \
        --vk-file "$vk_json" \
        --trace-result "$case_dir/trace_result.json" \
        --trace-verify "$case_dir/trace_verify.json" \
        --out "$case_dir/trace_publish.json" \
        >"$case_dir/trace_publish.out" 2>"$case_dir/trace_publish.err"; then
      fail_stage="publish_trace"
      err="$(tr -d '\n' < "$case_dir/trace_publish.err" | tail -c 400)"
      append_attempt
      return 0
    fi
    end="$(now_ms)"
    publish_ms="$(ms_diff "$start" "$end")"
    tx_hash="$(json_get "$case_dir/trace_publish.json" '.publishResponse.txHash')"
    trace_digest="$(json_get "$case_dir/trace_publish.json" '.traceDigest')"
    if [[ "$(json_get "$case_dir/trace_publish.json" '.ok')" != "1" ]]; then
      fail_stage="publish_trace"
      err="$(tr -d '\n' < "$case_dir/trace_publish.json" | tail -c 400)"
      append_attempt
      return 0
    fi
    publish_ok="1"
  else
    publish_ok="1"
  fi

  if [[ "$ENABLE_MAIN_CROSSCHECK" == "1" ]]; then
    log "run=$run_no attempt=$attempt_no main crosscheck"
    if "$BIN" --ttss_trace         --id "$id"         --bb "$BB"         --pirate "$PIRATE"         --ttss_state "$setup_json"         --committee_token "$TOKEN"         --workdir "$case_dir"         --project_root "$PROJECT_ROOT"         --vk_file "$vk_json"         --trace_result "$case_dir/main_trace_result.json"         --delta "$DELTA"         --challenge_count "$CHALLENGE_COUNT"         --max_queries "$MAX_QUERIES"         --leaked_indexes "$LEAKED_INDEXES"         >"$case_dir/main_ttss_trace.out" 2>"$case_dir/main_ttss_trace.err"       && "$BIN" --ttss_trace_publish         --id "$id"         --bb "$BB"         --ttss_state "$setup_json"         --workdir "$case_dir"         --project_root "$PROJECT_ROOT"         --vk_file "$vk_json"         --trace_result "$case_dir/main_trace_result.json"         >"$case_dir/main_ttss_trace_publish.out" 2>"$case_dir/main_ttss_trace_publish.err"; then
      main_crosscheck_ok="1"
    else
      fail_stage="main_crosscheck"
      err="main ttss_trace or ttss_trace_publish failed"
      append_attempt
      return 0
    fi
  else
    main_crosscheck_ok="1"
  fi

  if [[ "$STRICT_FULL_SET" == "1" ]]; then
    [[ "$exact_match" == "1" ]] || { fail_stage="accused_set_mismatch"; err="accused set not exact leaked set"; append_attempt; return 0; }
  else
    [[ "$hit_expected" == "1" ]] || { fail_stage="accused_set_miss"; err="accused set misses leaked set"; append_attempt; return 0; }
  fi

  status="pass"
  pass="1"
  append_attempt
}

check_health

pass_count=0
retry_count_total=0
run_no=1
while [[ "$run_no" -le "$RUNS" ]]; do
  final_attempt=0
  final_status="fail"
  final_pass="0"
  final_case_dir=""
  attempt_no=1
  while [[ "$attempt_no" -le $((MAX_RETRIES + 1)) ]]; do
    final_attempt="$attempt_no"
    case_dir="$OUT_DIR/run_$(printf '%03d' "$run_no")/attempt_$(printf '%02d' "$attempt_no")"
    final_case_dir="$case_dir"
    mkdir -p "$case_dir"
    run_one_attempt "$run_no" "$attempt_no" "$case_dir" || true
    status_last="$(tail -n 1 "$ATTEMPTS_CSV" | python3 -c 'import csv,sys; data=sys.stdin.read().strip(); row=next(csv.reader([data])) if data else []; print(row[2] if len(row)>2 else "")')"
    final_status="$status_last"
    if [[ "$status_last" == "pass" ]]; then
      final_pass="1"
      pass_count=$((pass_count + 1))
      break
    fi
    if [[ "$attempt_no" -le "$MAX_RETRIES" ]]; then
      retry_count_total=$((retry_count_total + 1))
      log "run=$run_no attempt=$attempt_no failed, retry after ${RETRY_SLEEP_SEC}s"
      sleep "$RETRY_SLEEP_SEC"
    fi
    attempt_no=$((attempt_no + 1))
  done

  last_row="$(tail -n 1 "$ATTEMPTS_CSV")"
  python3 - "$SUMMARY_CSV" "$run_no" "$final_attempt" "$final_status" "$final_pass" "$last_row" "$final_case_dir" <<'PY'
import csv, sys
summary_csv, run_no, final_attempt, final_status, final_pass, last_row, final_case_dir = sys.argv[1:]
row = next(csv.reader([last_row]))
out = [run_no, final_attempt, final_status, final_pass] + row[4:-1] + [final_case_dir]
with open(summary_csv, 'a', newline='') as f:
    csv.writer(f).writerow(out)
PY

  run_no=$((run_no + 1))
done

log "done: pass_count=${pass_count}/${RUNS}, retry_count_total=${retry_count_total}, out=${OUT_DIR}"
if [[ "$pass_count" -ne "$RUNS" ]]; then
  die "batch failed: pass_count=${pass_count}/${RUNS}"
fi
log "PASS"
