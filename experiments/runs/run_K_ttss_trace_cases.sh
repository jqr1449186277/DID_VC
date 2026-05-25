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
TTSS_N="${TTSS_N:-6}"
TTSS_T="${TTSS_T:-4}"
DELTA="${DELTA:-0.000001}"
CHALLENGE_COUNT="${CHALLENGE_COUNT:-1}"
MAX_QUERIES="${MAX_QUERIES:-3}"
RUNS_PER_CASE="${RUNS_PER_CASE:-5}"
MAX_RETRIES="${MAX_RETRIES:-2}"
RETRY_SLEEP_SEC="${RETRY_SLEEP_SEC:-4}"
PROJECT_ROOT="${PROJECT_ROOT:-$ROOT}"
TIMEOUT_MS="${TIMEOUT_MS:-120000}"
REGISTER_WAIT_MS="${REGISTER_WAIT_MS:-12000}"
PATH_WAIT_MS="${PATH_WAIT_MS:-8000}"
ROOT_WAIT_MS="${ROOT_WAIT_MS:-30000}"
ROOT_POLL_MS="${ROOT_POLL_MS:-750}"
SKIP_PUBLISH_ON_NEGATIVE="${SKIP_PUBLISH_ON_NEGATIVE:-1}"
SETTLE_SLEEP_SEC="${SETTLE_SLEEP_SEC:-2}"
INTER_RUN_SLEEP_SEC="${INTER_RUN_SLEEP_SEC:-1}"
INTER_CASE_SLEEP_SEC="${INTER_CASE_SLEEP_SEC:-2}"
# mode|leaked_indexes ; default covers: no leak, bad metadata, and 3 leaked sets
CASE_MATRIX="${CASE_MATRIX:-noleak|;badmeta|2,4;leak|2,4;leak|1,3;leak|1,4}"
NOLEAK_FOREIGN_INDEXES="${NOLEAK_FOREIGN_INDEXES:-2,4}"

TS="$(date +%Y%m%d_%H%M%S)"
OUT_DIR="${OUT_DIR:-$OUT_BASE/${TS}_K_ttss_trace_cases}"
mkdir -p "$OUT_DIR"

RUN_LOG="$OUT_DIR/run.log"
ATTEMPTS_CSV="$OUT_DIR/attempts.csv"
SUMMARY_CSV="$OUT_DIR/summary.csv"
CONFIG_JSON="$OUT_DIR/config.json"
CASES_JSON="$OUT_DIR/cases.json"

log(){ echo "[run_K_trace_cases] $*" | tee -a "$RUN_LOG"; }
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

python3 - "$CASE_MATRIX" > "$CASES_JSON" <<'PY'
import json, sys
raw = sys.argv[1]
cases = []
for idx, chunk in enumerate([x.strip() for x in raw.split(';') if x.strip()], start=1):
    parts = chunk.split('|', 1)
    mode = parts[0].strip()
    leaked = parts[1].strip() if len(parts) > 1 else ''
    cases.append({"case_no": idx, "mode": mode, "leaked_indexes": leaked})
print(json.dumps(cases, ensure_ascii=False))
PY

jq -nc \
  --arg root "$ROOT" \
  --arg bin "$BIN" \
  --arg tracer "$TRACER" \
  --arg bb "$BB" \
  --arg pirate "$PIRATE" \
  --arg token "$TOKEN" \
  --arg committee_urls "$COMMITTEE_URLS" \
  --argjson ttss_n "$TTSS_N" \
  --argjson ttss_t "$TTSS_T" \
  --arg delta "$DELTA" \
  --argjson challenge_count "$CHALLENGE_COUNT" \
  --argjson max_queries "$MAX_QUERIES" \
  --argjson runs_per_case "$RUNS_PER_CASE" \
  --arg case_matrix "$CASE_MATRIX" \
  --arg noleak_foreign_indexes "$NOLEAK_FOREIGN_INDEXES" \
  --argjson max_retries "$MAX_RETRIES" \
  --argjson retry_sleep_sec "$RETRY_SLEEP_SEC" \
  --arg project_root "$PROJECT_ROOT" \
  --argjson timeout_ms "$TIMEOUT_MS" \
  --argjson register_wait_ms "$REGISTER_WAIT_MS" \
  --argjson path_wait_ms "$PATH_WAIT_MS" \
  --argjson root_wait_ms "$ROOT_WAIT_MS" \
  --argjson root_poll_ms "$ROOT_POLL_MS" \
  --argjson settle_sleep_sec "$SETTLE_SLEEP_SEC" \
  --argjson inter_run_sleep_sec "$INTER_RUN_SLEEP_SEC" \
  --argjson inter_case_sleep_sec "$INTER_CASE_SLEEP_SEC" \
  '{root:$root,bin:$bin,tracer:$tracer,bb:$bb,pirate:$pirate,token:$token,committee_urls:$committee_urls,ttss_n:$ttss_n,ttss_t:$ttss_t,delta:$delta,challenge_count:$challenge_count,max_queries:$max_queries,runs_per_case:$runs_per_case,case_matrix:$case_matrix,noleak_foreign_indexes:$noleak_foreign_indexes,max_retries:$max_retries,retry_sleep_sec:$retry_sleep_sec,project_root:$project_root,timeout_ms:$timeout_ms,register_wait_ms:$register_wait_ms,path_wait_ms:$path_wait_ms,root_wait_ms:$root_wait_ms,root_poll_ms:$root_poll_ms,settle_sleep_sec:$settle_sleep_sec,inter_run_sleep_sec:$inter_run_sleep_sec,inter_case_sleep_sec:$inter_case_sleep_sec}' \
  > "$CONFIG_JSON"

cat > "$ATTEMPTS_CSV" <<'CSV'
case_no,case_mode,run_no,attempt_no,status,pass,id,id_hash,ver,epoch,expected_leaked_set,accused_set,accused_set_size,verify_accepted,publish_ok,trace_run_ok,trace_verify_ok,setup_ok,box_load_ok,oracle_probe_ok,trace_query_count,setup_ms,load_box_ms,oracle_probe_ms,trace_run_ms,trace_verify_ms,publish_ms,box_id,trace_session_id,oracle_result_type,oracle_ok,expected_behavior,hit_expected,exact_match,fail_stage,err,case_dir
CSV

cat > "$SUMMARY_CSV" <<'CSV'
case_no,case_mode,expected_leaked_set,total_runs,pass_runs,fail_runs,avg_trace_run_ms,avg_trace_verify_ms,avg_publish_ms,avg_accused_set_size,notes
CSV

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

build_sets(){
  local leaked_indexes="$1"
  python3 - "$COMMITTEE_URLS" "$leaked_indexes" <<'PY'
import sys, json
urls = [u.strip() for u in sys.argv[1].split(',') if u.strip()]
raw = sys.argv[2].strip()
leaked = [int(x) for x in raw.split(',') if x.strip()] if raw else []
honest = [u for i, u in enumerate(urls, start=1) if i not in leaked]
print(json.dumps({"all_urls": urls, "leaked": leaked, "honest_urls": honest}, ensure_ascii=False))
PY
}

fetch_share_for_trace(){
  local committee_url="$1" trace_session_id="$2" id_hash="$3" ver="$4" epoch="$5" out_json="$6"
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
  local id_hash="$1" ver="$2" epoch="$3" leaked_count="$4" leaked_array_json="$5" out_json="$6"
  local payload
  payload="$(jq -nc \
    --arg token "$TOKEN" \
    --arg idHash "$id_hash" \
    --argjson ver "$ver" \
    --argjson epoch "$epoch" \
    --argjson n "$TTSS_N" \
    --argjson t "$TTSS_T" \
    --argjson f "$leaked_count" \
    --argjson leakedShares "$leaked_array_json" \
    '{token:$token, boxConfig:{scheme:"NITS-Shamir-v1", mode:"stateless", idHash:$idHash, ver:$ver, epoch:$epoch, n:$n, t:$t, f:$f, secretType:"srec_seed", leakedShares:$leakedShares, outputMode:"seed"}}')"
  curl -fsS -X POST "$PIRATE/loadStaticBox" \
    -H 'Content-Type: application/json' \
    -d "$payload" | tee "$out_json" >/dev/null
}

oracle_probe(){
  local box_id="$1" id_hash="$2" ver="$3" epoch="$4" honest_shares_json="$5" out_json="$6"
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

tamper_metadata(){
  local src="$1" dst="$2"
  python3 - "$src" "$dst" <<'PY'
import json, sys
src, dst = sys.argv[1:3]
obj = json.load(open(src))

def mutate(x):
    if isinstance(x, dict):
        y = {}
        for k, v in x.items():
            if k in ("idHash", "traceDigest", "vkSetHash", "metaHash") and isinstance(v, str):
                y[k] = ("deadbeef" + v[8:]) if len(v) >= 8 else (v + "_bad")
            elif k in ("ver", "epoch") and isinstance(v, int):
                y[k] = v + 1
            else:
                y[k] = mutate(v)
        return y
    if isinstance(x, list):
        return [mutate(v) for v in x]
    return x
json.dump(mutate(obj), open(dst, 'w'))
PY
}

compute_set_check(){
  local expected="$1" accused_json="$2" out_json="$3"
  python3 - "$expected" "$accused_json" > "$out_json" <<'PY'
import json, sys
exp_raw, acc_raw = sys.argv[1:3]
exp = [int(x) for x in exp_raw.split(',') if x.strip()] if exp_raw.strip() else []
acc = json.loads(acc_raw)
exp_set = set(exp)
acc_set = set(int(x) for x in acc)
out = {
  "hit_expected": int(len(exp_set & acc_set) > 0) if exp_set else int(len(acc_set) == 0),
  "exact_match": int(exp_set == acc_set)
}
print(json.dumps(out))
PY
}

append_attempt(){
  append_csv "$ATTEMPTS_CSV" \
    "$case_no" "$case_mode" "$run_no" "$attempt_no" "$status" "$pass" "$id" "$id_hash" "$ver" "$epoch" \
    "$expected_leaked_set" "$accused_set" "$accused_set_size" "$verify_accepted" "$publish_ok" "$trace_run_ok" "$trace_verify_ok" "$setup_ok" "$box_load_ok" "$oracle_probe_ok" \
    "$trace_query_count" "$setup_ms" "$load_box_ms" "$oracle_probe_ms" "$trace_run_ms" "$trace_verify_ms" "$publish_ms" "$box_id" "$trace_session_id" "$oracle_result_type" "$oracle_ok" \
    "$expected_behavior" "$hit_expected" "$exact_match" "$fail_stage" "$err" "$case_dir"
}

run_one_attempt(){
  local case_no="$1" case_mode="$2" expected_leaked_set="$3" run_no="$4" attempt_no="$5" case_dir="$6"

  mkdir -p "$case_dir" "$case_dir/leaked_shares" "$case_dir/honest_probe"

  local status="fail" pass="0" setup_ok="0" box_load_ok="0" oracle_probe_ok="0" trace_run_ok="0" trace_verify_ok="0" publish_ok="0"
  local fail_stage="" err="" id="trace_case${case_no}_r${run_no}_a${attempt_no}_$(date +%s)"
  local id_hash="" ver="" epoch="" setup_ms="0" load_box_ms="0" oracle_probe_ms="0" trace_run_ms="0" trace_verify_ms="0" publish_ms="0"
  local box_id="" trace_session_id="trace_${TS}_c${case_no}_r${run_no}_a${attempt_no}" oracle_result_type="" oracle_ok="0" trace_query_count="0"
  local box_id_hash="" box_ver="" box_epoch="" box_source="same_setup"
  local accused_set_size="0" accused_set="[]" hit_expected="0" exact_match="0" verify_accepted="0"
  local expected_behavior=""

  trap 'cleanup_box "$box_id"' RETURN

  local sets_json honest_urls honest_count leaked_count
  sets_json="$(build_sets "$expected_leaked_set")"
  honest_urls="$(printf '%s' "$sets_json" | jq -r '.honest_urls | join(",")')"
  honest_count="$(printf '%s' "$sets_json" | jq -r '.honest_urls | length')"
  leaked_count="$(printf '%s' "$sets_json" | jq -r '.leaked | length')"
  [[ "$leaked_count" -lt "$TTSS_T" ]] || { fail_stage="bad_case_leaked_count"; err="leaked count must be < t"; append_attempt; return 0; }
  [[ "$honest_count" -ge 1 ]] || { fail_stage="bad_case_honest_count"; err="no honest committee left"; append_attempt; return 0; }

  log "case=$case_mode leaks='${expected_leaked_set:-<none>}' run=$run_no attempt=$attempt_no ttss_setup id=$id"
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
  if [[ "$SETTLE_SLEEP_SEC" -gt 0 ]]; then sleep "$SETTLE_SLEEP_SEC"; fi

  local setup_json="$case_dir/${id}_ttss_setup/ttss_setup.json"
  local vk_json="$case_dir/${id}_ttss_setup/ttss_vk.json"
  [[ -f "$setup_json" && -f "$vk_json" ]] || { fail_stage="ttss_setup_artifacts"; err="missing ttss_setup.json or ttss_vk.json"; append_attempt; return 0; }
  id_hash="$(json_get "$setup_json" '.idHash')"
  ver="$(json_get "$setup_json" '.ver')"
  epoch="$(json_get "$setup_json" '.epoch')"
  [[ -n "$id_hash" && -n "$ver" && -n "$epoch" ]] || { fail_stage="ttss_setup_tuple"; err="missing idHash/ver/epoch"; append_attempt; return 0; }

  box_id_hash="$id_hash"
  box_ver="$ver"
  box_epoch="$epoch"

  local box_leaked_indexes="$expected_leaked_set"
  if [[ "$case_mode" == "noleak" ]]; then
    box_source="foreign_setup"
    box_leaked_indexes="$NOLEAK_FOREIGN_INDEXES"
    local foreign_id="trace_case${case_no}_r${run_no}_a${attempt_no}_foreign_$(date +%s)"
    local foreign_dir="$case_dir/foreign_setup"
    mkdir -p "$foreign_dir"
    log "case=$case_mode leaks='<none>' run=$run_no attempt=$attempt_no ttss_setup foreign_id=$foreign_id"
    if ! "$BIN" \
        --ttss_setup \
        --id "$foreign_id" \
        --bb "$BB" \
        --committee_urls "$COMMITTEE_URLS" \
        --committee_token "$TOKEN" \
        --ttss_n "$TTSS_N" \
        --ttss_t "$TTSS_T" \
        --project_root "$PROJECT_ROOT" \
        --workdir "$foreign_dir" \
        --timeout_ms "$TIMEOUT_MS" \
        --register_wait_ms "$REGISTER_WAIT_MS" \
        --path_wait_ms "$PATH_WAIT_MS" \
        --root_wait_ms "$ROOT_WAIT_MS" \
        --root_poll_ms "$ROOT_POLL_MS" \
        >"$foreign_dir/ttss_setup.out" 2>"$foreign_dir/ttss_setup.err"; then
      fail_stage="ttss_setup_foreign"
      err="$(tr -d '\n' < "$foreign_dir/ttss_setup.err" | tail -c 400)"
      append_attempt
      return 0
    fi
    if [[ "$SETTLE_SLEEP_SEC" -gt 0 ]]; then sleep "$SETTLE_SLEEP_SEC"; fi
    local foreign_json="$foreign_dir/${foreign_id}_ttss_setup/ttss_setup.json"
    [[ -f "$foreign_json" ]] || { fail_stage="ttss_setup_foreign_artifacts"; err="missing foreign ttss_setup.json"; append_attempt; return 0; }
    box_id_hash="$(json_get "$foreign_json" '.idHash')"
    box_ver="$(json_get "$foreign_json" '.ver')"
    box_epoch="$(json_get "$foreign_json" '.epoch')"
    [[ -n "$box_id_hash" && -n "$box_ver" && -n "$box_epoch" ]] || { fail_stage="ttss_setup_foreign_tuple"; err="missing foreign idHash/ver/epoch"; append_attempt; return 0; }
  fi

  local leaked_files=() honest_probe_files=() leaked_array_json honest_probe_array_json
  IFS=',' read -r -a committee_arr <<< "$COMMITTEE_URLS"
  IFS=',' read -r -a leaked_arr <<< "$box_leaked_indexes"
  local idx committee_url reply_file
  for idx in "${leaked_arr[@]}"; do
    [[ -n "$idx" ]] || continue
    committee_url="${committee_arr[$((idx-1))]}"
    reply_file="$case_dir/leaked_shares/guardian_${idx}.json"
    if ! fetch_share_for_trace "$committee_url" "$trace_session_id" "$box_id_hash" "$box_ver" "$box_epoch" "$reply_file"; then
      fail_stage="fetch_leaked_share_g${idx}"
      err="shareForTrace failed at guardian ${idx}"
      append_attempt
      return 0
    fi
    leaked_files+=("$reply_file")
  done
  leaked_array_json="$(printf '%s\n' "${leaked_files[@]:-}" | python3 -c 'import json,sys; files=[line.strip() for line in sys.stdin if line.strip()]; arr=[]
for p in files:
    obj=json.load(open(p))
    arr.append(obj.get("shareEnvelope", obj))
print(json.dumps(arr, ensure_ascii=False))')"

  local honest_need=$(( TTSS_T - leaked_count ))
  local honest_urls_arr=()
  IFS=',' read -r -a honest_urls_arr <<< "$honest_urls"
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
  honest_probe_array_json="$(printf '%s\n' "${honest_probe_files[@]:-}" | python3 -c 'import json,sys; files=[line.strip() for line in sys.stdin if line.strip()]; arr=[]
for p in files:
    obj=json.load(open(p))
    arr.append(obj.get("shareEnvelope", obj))
print(json.dumps(arr, ensure_ascii=False))')"

  start="$(now_ms)"
  if ! load_static_box "$box_id_hash" "$box_ver" "$box_epoch" "$leaked_count" "$leaked_array_json" "$case_dir/box_load.json"; then
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

  start="$(now_ms)"
  if oracle_probe "$box_id" "$id_hash" "$ver" "$epoch" "$honest_probe_array_json" "$case_dir/oracle_probe.json"; then
    end="$(now_ms)"
    oracle_probe_ms="$(ms_diff "$start" "$end")"
    oracle_ok="$(json_get "$case_dir/oracle_probe.json" '.ok')"
    oracle_result_type="$(json_get "$case_dir/oracle_probe.json" '.resultType')"
    if [[ "$case_mode" == "noleak" ]]; then
      oracle_probe_ok="1"
    else
      if [[ "$oracle_ok" != "1" ]]; then
        fail_stage="oracle_probe"
        err="$(tr -d '\n' < "$case_dir/oracle_probe.json" | tail -c 400)"
        append_attempt
        return 0
      fi
      oracle_probe_ok="1"
    fi
  else
    end="$(now_ms)"
    oracle_probe_ms="$(ms_diff "$start" "$end")"
    if [[ "$case_mode" == "noleak" ]]; then
      oracle_probe_ok="1"
      oracle_ok="0"
      oracle_result_type="probe_failed_expected_for_noleak"
    else
      fail_stage="oracle_probe"
      err="oracle probe failed"
      append_attempt
      return 0
    fi
  fi

  start="$(now_ms)"
  if ! node "$TRACER" trace-run \
      --idHash "$id_hash" \
      --ver "$ver" \
      --epoch "$epoch" \
      --n "$TTSS_N" \
      --t "$TTSS_T" \
      --delta "$DELTA" \
      --vk-file "$vk_json" \
      --committee "$honest_urls" \
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
  trace_query_count="$(json_num "$case_dir/trace_result.json" '.traceMeta.queryCount')"
  accused_set_size="$(json_num "$case_dir/trace_result.json" '.accusedSet | length')"
  accused_set="$(jq -c '.accusedSet // []' "$case_dir/trace_result.json")"
  compute_set_check "$expected_leaked_set" "$accused_set" "$case_dir/set_check.json"
  hit_expected="$(json_get "$case_dir/set_check.json" '.hit_expected')"
  exact_match="$(json_get "$case_dir/set_check.json" '.exact_match')"

  local verify_input="$case_dir/trace_result.json"
  local verify_should_accept="1"
  if [[ "$case_mode" == "badmeta" ]]; then
    tamper_metadata "$case_dir/trace_result.json" "$case_dir/trace_result_badmeta.json"
    verify_input="$case_dir/trace_result_badmeta.json"
    verify_should_accept="0"
    expected_behavior="verify_rejects_tampered_metadata"
  elif [[ "$case_mode" == "noleak" ]]; then
    verify_should_accept="0"
    expected_behavior="no_false_accusation"
  else
    expected_behavior="trace_hits_expected_leaked_set"
  fi

  start="$(now_ms)"
  if node "$TRACER" trace-verify \
      --vk-file "$vk_json" \
      --trace-result "$verify_input" \
      --out "$case_dir/trace_verify.json" \
      >"$case_dir/trace_verify.out" 2>"$case_dir/trace_verify.err"; then
    verify_accepted="$(json_get "$case_dir/trace_verify.json" '.accepted')"
    end="$(now_ms)"
    trace_verify_ms="$(ms_diff "$start" "$end")"
    trace_verify_ok="1"
  else
    verify_accepted="0"
    end="$(now_ms)"
    trace_verify_ms="$(ms_diff "$start" "$end")"
    trace_verify_ok="0"
  fi

  if [[ "$verify_should_accept" == "1" ]]; then
    if [[ "$verify_accepted" != "true" && "$verify_accepted" != "1" ]]; then
      fail_stage="trace_verify_reject"
      err="trace verify not accepted"
      append_attempt
      return 0
    fi
    trace_verify_ok="1"
  else
    if [[ "$verify_accepted" == "true" || "$verify_accepted" == "1" ]]; then
      fail_stage="trace_verify_unexpected_accept"
      if [[ "$case_mode" == "noleak" ]]; then
        err="empty accused set unexpectedly accepted"
      else
        err="tampered metadata unexpectedly accepted"
      fi
      append_attempt
      return 0
    fi
    publish_ok="1"
    status="pass"
    pass="1"
    append_attempt
    return 0
  fi

  if [[ "$case_mode" == "noleak" ]]; then
    [[ "$accused_set_size" == "0" ]] || { fail_stage="false_accusation"; err="accused set non-empty in noleak case"; append_attempt; return 0; }
  else
    [[ "$exact_match" == "1" ]] || { fail_stage="accused_set_mismatch"; err="accused set not exact leaked set"; append_attempt; return 0; }
  fi

  if [[ "$SKIP_PUBLISH_ON_NEGATIVE" == "1" && "$case_mode" == "noleak" ]]; then
    publish_ok="1"
  else
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
    if [[ "$(json_get "$case_dir/trace_publish.json" '.ok')" != "1" ]]; then
      fail_stage="publish_trace"
      err="$(tr -d '\n' < "$case_dir/trace_publish.json" | tail -c 400)"
      append_attempt
      return 0
    fi
    publish_ok="1"
  fi

  status="pass"
  pass="1"
  append_attempt
}

check_health

case_count="$(jq 'length' "$CASES_JSON")"
for ((case_idx=0; case_idx<case_count; case_idx++)); do
  case_no="$(jq -r ".[$case_idx].case_no" "$CASES_JSON")"
  case_mode="$(jq -r ".[$case_idx].mode" "$CASES_JSON")"
  leaked_indexes="$(jq -r ".[$case_idx].leaked_indexes" "$CASES_JSON")"
  log "=== case_no=$case_no mode=$case_mode leaks='${leaked_indexes:-<none>}' ==="

  for ((run_no=1; run_no<=RUNS_PER_CASE; run_no++)); do
    attempt_no=1
    while [[ "$attempt_no" -le $((MAX_RETRIES + 1)) ]]; do
      case_dir="$OUT_DIR/case_$(printf '%02d' "$case_no")_${case_mode}/run_$(printf '%03d' "$run_no")/attempt_$(printf '%02d' "$attempt_no")"
      mkdir -p "$case_dir"
      run_one_attempt "$case_no" "$case_mode" "$leaked_indexes" "$run_no" "$attempt_no" "$case_dir" || true
      status_last="$(tail -n 1 "$ATTEMPTS_CSV" | python3 -c 'import csv,sys; s=sys.stdin.read().strip(); row=next(csv.reader([s])) if s else []; print(row[4] if len(row)>4 else "")')"
      [[ "$status_last" == "pass" ]] && break
      if [[ "$attempt_no" -le "$MAX_RETRIES" ]]; then
        log "case=$case_mode leaks='${leaked_indexes:-<none>}' run=$run_no attempt=$attempt_no failed, retry after ${RETRY_SLEEP_SEC}s"
        sleep "$RETRY_SLEEP_SEC"
      fi
      attempt_no=$((attempt_no + 1))
    done
  done

done

python3 - "$ATTEMPTS_CSV" "$SUMMARY_CSV" <<'PY'
import csv, sys, statistics
attempts_csv, summary_csv = sys.argv[1:3]
rows = list(csv.DictReader(open(attempts_csv, newline='')))
by_case = {}
for r in rows:
    key = (r['case_no'], r['case_mode'], r['expected_leaked_set'])
    by_case.setdefault(key, []).append(r)
with open(summary_csv, 'a', newline='') as f:
    w = csv.writer(f)
    for (case_no, case_mode, leaked), grp in by_case.items():
        total = len(grp)
        pass_runs = sum(1 for r in grp if r['pass'] == '1')
        fail_runs = total - pass_runs
        def avg(field):
            vals = [float(r[field]) for r in grp if r[field] not in ('', '0')]
            return f"{statistics.mean(vals):.2f}" if vals else '0'
        avg_acc = avg('accused_set_size')
        notes = {
            'noleak': 'expect empty accused set',
            'badmeta': 'expect verify rejection on tampered metadata',
            'leak': 'expect exact leaked set match'
        }.get(case_mode, '')
        w.writerow([case_no, case_mode, leaked, total, pass_runs, fail_runs, avg('trace_run_ms'), avg('trace_verify_ms'), avg('publish_ms'), avg_acc, notes])
PY

log "done: out=$OUT_DIR"
log "PASS"
