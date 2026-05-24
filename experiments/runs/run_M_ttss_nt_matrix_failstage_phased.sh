#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

ROOT="${ROOT:-${PROJECT_ROOT:-$(cd "$SCRIPT_DIR/../.." && pwd)}}"
OUT_BASE="${OUT_BASE:-$ROOT/results}"
RUN_J_SCRIPT="${RUN_J_SCRIPT:-$ROOT/experiments/runs/run_J_ttss_recover_batch.sh}"
RUN_K_SCRIPT="${RUN_K_SCRIPT:-$ROOT/experiments/runs/run_K_ttss_trace_batch.sh}"
BIN="${BIN:-$ROOT/build/did_demo_zk}"
BB="${BB:-http://127.0.0.1:3000}"
PIRATE="${PIRATE:-http://127.0.0.1:4000}"
TOKEN="${TOKEN:-demo-token}"
PROJECT_ROOT="${PROJECT_ROOT:-$ROOT}"
COMMITTEE_URLS="${COMMITTEE_URLS:-http://127.0.0.1:8001,http://127.0.0.1:8002,http://127.0.0.1:8003,http://127.0.0.1:8004,http://127.0.0.1:8005,http://127.0.0.1:8006,http://127.0.0.1:8007,http://127.0.0.1:8008,http://127.0.0.1:8009,http://127.0.0.1:8010}"

ENABLE_RECOVER="${ENABLE_RECOVER:-1}"
ENABLE_TRACE="${ENABLE_TRACE:-1}"
RECOVER_RUNS="${RECOVER_RUNS:-3}"
TRACE_RUNS="${TRACE_RUNS:-3}"
RECOVER_MAX_RETRIES="${RECOVER_MAX_RETRIES:-2}"
TRACE_MAX_RETRIES="${TRACE_MAX_RETRIES:-2}"
TRACE_MAX_QUERIES="${TRACE_MAX_QUERIES:-3}"
TRACE_DELTA="${TRACE_DELTA:-0.000001}"
TRACE_STRICT_FULL_SET="${TRACE_STRICT_FULL_SET:-1}"
TRACE_ENABLE_MAIN_CROSSCHECK="${TRACE_ENABLE_MAIN_CROSSCHECK:-0}"
TRACE_SKIP_PUBLISH="${TRACE_SKIP_PUBLISH:-0}"
RERUN_DUPLICATE_POINTS="${RERUN_DUPLICATE_POINTS:-0}"
POINT_SLEEP_SEC="${POINT_SLEEP_SEC:-0}"

TIMEOUT_MS="${TIMEOUT_MS:-30000}"
REGISTER_WAIT_MS="${REGISTER_WAIT_MS:-120000}"
PATH_WAIT_MS="${PATH_WAIT_MS:-120000}"
ROOT_WAIT_MS="${ROOT_WAIT_MS:-60000}"
ROOT_POLL_MS="${ROOT_POLL_MS:-300}"

GROUP_FIXED_T4="${GROUP_FIXED_T4:-4:4,5:4,6:4,7:4,8:4}"
GROUP_FIXED_N6="${GROUP_FIXED_N6:-6:2,6:3,6:4,6:5,6:6}"
GROUP_SELECTED="${GROUP_SELECTED:-3:2,6:4,8:5}"

TS="$(date +%Y%m%d_%H%M%S)"
OUT_DIR="${OUT_DIR:-$OUT_BASE/${TS}_M_ttss_nt_matrix}"
RUN_LOG="$OUT_DIR/run.log"
POINT_MAP_CSV="$OUT_DIR/point_map.csv"
AGG_CSV="$OUT_DIR/aggregate.csv"
FAIL_STAGE_BY_POINT_CSV="$OUT_DIR/fail_stage_by_point.csv"
FAIL_STAGE_OVERALL_CSV="$OUT_DIR/fail_stage_overall.csv"
mkdir -p "$OUT_DIR"

log(){ echo "[run_M_nt_matrix] $*" | tee -a "$RUN_LOG"; }
die(){ log "FATAL: $*"; exit 1; }
require_cmd(){ command -v "$1" >/dev/null 2>&1 || die "missing command: $1"; }

require_cmd jq
require_cmd curl
require_cmd python3
[[ -f "$RUN_J_SCRIPT" ]] || die "missing script: $RUN_J_SCRIPT"
[[ -f "$RUN_K_SCRIPT" ]] || die "missing script: $RUN_K_SCRIPT"
[[ -x "$BIN" ]] || die "BIN not executable: $BIN"

check_health(){
  log "health check: bb"
  curl -fsS "$BB/health" >/dev/null || die "bb health failed: $BB/health"
  if [[ "$ENABLE_TRACE" == "1" ]]; then
    log "health check: pirate"
    curl -fsS "$PIRATE/health" >/dev/null || die "pirate health failed: $PIRATE/health"
  fi
  log "health check: committee"
  IFS=',' read -r -a committee_arr <<< "$COMMITTEE_URLS"
  local u
  for u in "${committee_arr[@]}"; do
    curl -fsS "$u/health" >/dev/null || die "committee health failed: $u/health"
  done
}

available_committee_count(){
  python3 - "$COMMITTEE_URLS" <<'PY'
import sys
urls=[u.strip() for u in sys.argv[1].split(',') if u.strip()]
print(len(urls))
PY
}

subset_committee_urls(){
  python3 - "$COMMITTEE_URLS" "$1" <<'PY'
import sys
urls=[u.strip() for u in sys.argv[1].split(',') if u.strip()]
n=int(sys.argv[2])
if n > len(urls):
    raise SystemExit(f"need {n} committee urls, have {len(urls)}")
print(','.join(urls[:n]))
PY
}

choose_leaked_indexes(){
  python3 - "$1" "$2" <<'PY'
import sys
n=int(sys.argv[1]); t=int(sys.argv[2])
count=min(2, max(1, t-1))
cands=[2,4,6,8,1,3,5,7,9,10]
out=[]
for c in cands:
    if c <= n and c not in out:
        out.append(c)
    if len(out) == count:
        break
if len(out) < count:
    raise SystemExit(f"cannot choose {count} leaked indexes for n={n}, t={t}")
print(','.join(str(x) for x in out))
PY
}

compute_challenge_count(){
  python3 - "$1" "$2" <<'PY'
import sys
n=int(sys.argv[1]); t=int(sys.argv[2])
count=min(2, max(1, t-1))
print(max(1, t - count))
PY
}

summarize_csv(){
  python3 - "$1" "$2" <<'PY'
import csv, json, sys
from statistics import mean
path = sys.argv[1]
kind = sys.argv[2]
with open(path, newline='', encoding='utf-8') as f:
    rows = list(csv.DictReader(f))
if not rows:
    print(json.dumps({"runs":0,"pass_count":0,"pass_rate":0.0}))
    raise SystemExit(0)
pass_rows = [r for r in rows if str(r.get('pass','0')).strip().lower() in {'1','true','yes','y','pass'}]
res = {
    'runs': len(rows),
    'pass_count': len(pass_rows),
    'pass_rate': round((len(pass_rows) / len(rows)) if rows else 0.0, 6),
}
def avg_of(key):
    vals=[]
    for r in pass_rows:
        v=str(r.get(key,'')).strip()
        if v == '':
            continue
        try:
            vals.append(float(v))
        except ValueError:
            pass
    return round(mean(vals), 3) if vals else 0.0
if kind == 'recover':
    for key in ['setup_ms','recover_ms','rotate_ms','share_count_used']:
        res['avg_' + key] = avg_of(key)
else:
    for key in ['setup_ms','load_box_ms','oracle_probe_ms','trace_run_ms','trace_verify_ms','publish_ms','trace_query_count','accused_set_size']:
        res['avg_' + key] = avg_of(key)
print(json.dumps(res, ensure_ascii=False))
PY
}

summarize_fail_stages(){
  python3 - "$1" "$2" <<'PY'
import csv, json, sys
from collections import Counter
attempts_path = sys.argv[1]
summary_path = sys.argv[2]
res = {
    'attempt_failed_total': 0,
    'attempt_fail_stage_top': '',
    'attempt_fail_stage_top_count': 0,
    'final_failed_runs': 0,
    'final_fail_stage_top': '',
    'final_fail_stage_top_count': 0,
    'attempt_counts': {},
    'final_counts': {},
}
def normalize_stage(v):
    v = (v or '').strip()
    return v if v else '(empty)'

def is_pass(v):
    return str(v or '').strip().lower() in {'1','true','yes','y','pass'}
try:
    with open(attempts_path, newline='', encoding='utf-8') as f:
        rows = list(csv.DictReader(f))
except FileNotFoundError:
    rows = []
attempt_counter = Counter()
for r in rows:
    if is_pass(r.get('pass', '0')):
        continue
    stage = normalize_stage(r.get('fail_stage', ''))
    attempt_counter[stage] += 1
if attempt_counter:
    stage, cnt = sorted(attempt_counter.items(), key=lambda kv: (-kv[1], kv[0]))[0]
    res['attempt_failed_total'] = sum(attempt_counter.values())
    res['attempt_fail_stage_top'] = stage
    res['attempt_fail_stage_top_count'] = cnt
    res['attempt_counts'] = dict(sorted(attempt_counter.items(), key=lambda kv: (-kv[1], kv[0])))
try:
    with open(summary_path, newline='', encoding='utf-8') as f:
        rows = list(csv.DictReader(f))
except FileNotFoundError:
    rows = []
final_counter = Counter()
for r in rows:
    if is_pass(r.get('pass', '0')):
        continue
    stage = normalize_stage(r.get('fail_stage', ''))
    final_counter[stage] += 1
if final_counter:
    stage, cnt = sorted(final_counter.items(), key=lambda kv: (-kv[1], kv[0]))[0]
    res['final_failed_runs'] = sum(final_counter.values())
    res['final_fail_stage_top'] = stage
    res['final_fail_stage_top_count'] = cnt
    res['final_counts'] = dict(sorted(final_counter.items(), key=lambda kv: (-kv[1], kv[0])))
print(json.dumps(res, ensure_ascii=False))
PY
}

write_headers(){
  cat > "$POINT_MAP_CSV" <<'CSV'
group_name,group_order,point_order,n,t,point_key,point_dir,reuse_from
CSV
  cat > "$AGG_CSV" <<'CSV'
group_name,group_order,point_order,n,t,point_key,leaked_indexes,challenge_count,max_queries,recover_status,recover_runs,recover_pass_count,recover_pass_rate,avg_recover_setup_ms,avg_recover_ms,avg_rotate_ms,avg_share_count_used,recover_failed_attempts,recover_attempt_fail_stage_top,recover_attempt_fail_stage_top_count,recover_final_failed_runs,recover_final_fail_stage_top,recover_final_fail_stage_top_count,trace_status,trace_runs,trace_pass_count,trace_pass_rate,avg_trace_setup_ms,avg_load_box_ms,avg_oracle_probe_ms,avg_trace_run_ms,avg_trace_verify_ms,avg_publish_ms,avg_trace_query_count,avg_accused_set_size,trace_failed_attempts,trace_attempt_fail_stage_top,trace_attempt_fail_stage_top_count,trace_final_failed_runs,trace_final_fail_stage_top,trace_final_fail_stage_top_count,point_dir,note
CSV
  cat > "$FAIL_STAGE_BY_POINT_CSV" <<'CSV'
group_name,group_order,point_order,n,t,point_key,kind,scope,fail_stage,count,point_dir
CSV
  cat > "$FAIL_STAGE_OVERALL_CSV" <<'CSV'
kind,scope,fail_stage,count
CSV
}

append_point_map(){
  python3 - "$POINT_MAP_CSV" "$@" <<'PY'
import csv, sys
with open(sys.argv[1], 'a', newline='') as f:
    csv.writer(f).writerow(sys.argv[2:])
PY
}

append_agg(){
  python3 - "$AGG_CSV" "$@" <<'PY'
import csv, sys
with open(sys.argv[1], 'a', newline='') as f:
    csv.writer(f).writerow(sys.argv[2:])
PY
}

append_fail_stage_by_point(){
  python3 - "$FAIL_STAGE_BY_POINT_CSV" "$@" <<'PY'
import csv, sys
with open(sys.argv[1], 'a', newline='') as f:
    csv.writer(f).writerow(sys.argv[2:])
PY
}

emit_fail_stage_rows(){
  local group_name="$1" group_order="$2" point_order="$3" n="$4" t="$5" point_key="$6" kind="$7" point_dir="$8" json="$9"
  python3 - "$group_name" "$group_order" "$point_order" "$n" "$t" "$point_key" "$kind" "$point_dir" "$json" <<'PY'
import json, sys
(group_name, group_order, point_order, n, t, point_key, kind, point_dir, payload) = sys.argv[1:]
obj = json.loads(payload)
for scope_key, scope_name in [('attempt_counts', 'attempt'), ('final_counts', 'final_run')]:
    for fail_stage, count in obj.get(scope_key, {}).items():
        print('\t'.join([group_name, group_order, point_order, n, t, point_key, kind, scope_name, fail_stage, str(count), point_dir]))
PY
}

declare -A POINT_MAP_SEEN

run_point(){
  local group_name="$1" group_order="$2" point_order="$3" n="$4" t="$5" action="${6:-summarize}"
  local point_key="n${n}_t${t}"
  local point_dir="$OUT_DIR/points/$point_key"
  local subset_urls leaked_indexes challenge_count
  subset_urls="$(subset_committee_urls "$n")" || die "subset_committee_urls failed for $point_key"
  leaked_indexes="$(choose_leaked_indexes "$n" "$t")" || die "choose_leaked_indexes failed for $point_key"
  challenge_count="$(compute_challenge_count "$n" "$t")" || die "compute_challenge_count failed for $point_key"

  case "$action" in
    recover)
      mkdir -p "$point_dir"
      if [[ "$ENABLE_RECOVER" == "1" ]]; then
        local recover_dir="$point_dir/recover"
        if [[ -d "$recover_dir" && "$RERUN_DUPLICATE_POINTS" != "1" ]]; then
          log "reuse recover point $point_key for group=$group_name"
        else
          rm -rf "$recover_dir"
          mkdir -p "$recover_dir"
          log "recover phase point group=$group_name n=$n t=$t"
          if ROOT="$ROOT" BIN="$BIN" OUT_DIR="$recover_dir" OUT_BASE="$OUT_BASE" BB="$BB" TOKEN="$TOKEN" \
             COMMITTEE_URLS="$subset_urls" RUNS="$RECOVER_RUNS" MAX_RETRIES="$RECOVER_MAX_RETRIES" TTSS_N="$n" TTSS_T="$t" \
             PROJECT_ROOT="$PROJECT_ROOT" TIMEOUT_MS="$TIMEOUT_MS" REGISTER_WAIT_MS="$REGISTER_WAIT_MS" PATH_WAIT_MS="$PATH_WAIT_MS" \
             ROOT_WAIT_MS="$ROOT_WAIT_MS" ROOT_POLL_MS="$ROOT_POLL_MS" bash "$RUN_J_SCRIPT" >>"$RUN_LOG" 2>&1; then
            rm -f "$point_dir/.recover_failed"
          else
            touch "$point_dir/.recover_failed"
          fi
        fi
        if [[ "$POINT_SLEEP_SEC" -gt 0 ]]; then
          log "sleep ${POINT_SLEEP_SEC}s after recover point $point_key"
          sleep "$POINT_SLEEP_SEC"
        fi
      fi
      return 0
      ;;
    trace)
      mkdir -p "$point_dir"
      if [[ "$ENABLE_TRACE" == "1" ]]; then
        local trace_dir="$point_dir/trace"
        if [[ -d "$trace_dir" && "$RERUN_DUPLICATE_POINTS" != "1" ]]; then
          log "reuse trace point $point_key for group=$group_name"
        else
          rm -rf "$trace_dir"
          mkdir -p "$trace_dir"
          log "trace phase point group=$group_name n=$n t=$t leaked=$leaked_indexes challenge_count=$challenge_count"
          if ROOT="$ROOT" BIN="$BIN" OUT_DIR="$trace_dir" OUT_BASE="$OUT_BASE" BB="$BB" PIRATE="$PIRATE" TOKEN="$TOKEN" \
             COMMITTEE_URLS="$subset_urls" RUNS="$TRACE_RUNS" MAX_RETRIES="$TRACE_MAX_RETRIES" TTSS_N="$n" TTSS_T="$t" \
             PROJECT_ROOT="$PROJECT_ROOT" CHALLENGE_COUNT="$challenge_count" MAX_QUERIES="$TRACE_MAX_QUERIES" \
             LEAKED_INDEXES="$leaked_indexes" ENABLE_MAIN_CROSSCHECK="$TRACE_ENABLE_MAIN_CROSSCHECK" STRICT_FULL_SET="$TRACE_STRICT_FULL_SET" \
             DELTA="$TRACE_DELTA" SKIP_PUBLISH="$TRACE_SKIP_PUBLISH" TIMEOUT_MS="$TIMEOUT_MS" REGISTER_WAIT_MS="$REGISTER_WAIT_MS" \
             PATH_WAIT_MS="$PATH_WAIT_MS" ROOT_WAIT_MS="$ROOT_WAIT_MS" ROOT_POLL_MS="$ROOT_POLL_MS" bash "$RUN_K_SCRIPT" >>"$RUN_LOG" 2>&1; then
            rm -f "$point_dir/.trace_failed"
          else
            touch "$point_dir/.trace_failed"
          fi
        fi
        if [[ "$POINT_SLEEP_SEC" -gt 0 ]]; then
          log "sleep ${POINT_SLEEP_SEC}s after trace point $point_key"
          sleep "$POINT_SLEEP_SEC"
        fi
      fi
      return 0
      ;;
    summarize)
      ;;
    *)
      die "bad run_point action: $action"
      ;;
  esac

  local reuse_from="" note=""
  if [[ -n "${POINT_MAP_SEEN[$point_key]+x}" && "$RERUN_DUPLICATE_POINTS" != "1" ]]; then
    reuse_from="$point_dir"
  fi
  POINT_MAP_SEEN["$point_key"]=1

  [[ -f "$point_dir/.recover_failed" ]] && note="${note}recover_failed;"
  [[ -f "$point_dir/.trace_failed" ]] && note="${note}trace_failed;"

  append_point_map "$group_name" "$group_order" "$point_order" "$n" "$t" "$point_key" "$point_dir" "$reuse_from"

  local recover_status="skip" recover_runs="0" recover_pass_count="0" recover_pass_rate="0" avg_recover_setup_ms="0" avg_recover_ms="0" avg_rotate_ms="0" avg_share_count_used="0"
  local recover_failed_attempts="0" recover_attempt_fail_stage_top="" recover_attempt_fail_stage_top_count="0" recover_final_failed_runs="0" recover_final_fail_stage_top="" recover_final_fail_stage_top_count="0"
  local trace_status="skip" trace_runs="0" trace_pass_count="0" trace_pass_rate="0" avg_trace_setup_ms="0" avg_load_box_ms="0" avg_oracle_probe_ms="0" avg_trace_run_ms="0" avg_trace_verify_ms="0" avg_publish_ms="0" avg_trace_query_count="0" avg_accused_set_size="0"
  local trace_failed_attempts="0" trace_attempt_fail_stage_top="" trace_attempt_fail_stage_top_count="0" trace_final_failed_runs="0" trace_final_fail_stage_top="" trace_final_fail_stage_top_count="0"

  if [[ "$ENABLE_RECOVER" == "1" ]]; then
    local recover_summary="$point_dir/recover/summary.csv"
    local recover_attempts="$point_dir/recover/attempts.csv"
    if [[ -f "$recover_summary" ]]; then
      local rjson
      rjson="$(summarize_csv "$recover_summary" recover)"
      recover_runs="$(jq -r '.runs // 0' <<<"$rjson")"
      recover_pass_count="$(jq -r '.pass_count // 0' <<<"$rjson")"
      recover_pass_rate="$(jq -r '.pass_rate // 0' <<<"$rjson")"
      avg_recover_setup_ms="$(jq -r '.avg_setup_ms // 0' <<<"$rjson")"
      avg_recover_ms="$(jq -r '.avg_recover_ms // 0' <<<"$rjson")"
      avg_rotate_ms="$(jq -r '.avg_rotate_ms // 0' <<<"$rjson")"
      avg_share_count_used="$(jq -r '.avg_share_count_used // 0' <<<"$rjson")"
      if [[ "$recover_pass_count" == "$recover_runs" && "$recover_runs" != "0" ]]; then recover_status="pass"; else recover_status="partial"; fi
    else
      recover_status="fail"
    fi
    if [[ -f "$recover_attempts" || -f "$recover_summary" ]]; then
      local rfjson
      rfjson="$(summarize_fail_stages "$recover_attempts" "$recover_summary")"
      recover_failed_attempts="$(jq -r '.attempt_failed_total // 0' <<<"$rfjson")"
      recover_attempt_fail_stage_top="$(jq -r '.attempt_fail_stage_top // ""' <<<"$rfjson")"
      recover_attempt_fail_stage_top_count="$(jq -r '.attempt_fail_stage_top_count // 0' <<<"$rfjson")"
      recover_final_failed_runs="$(jq -r '.final_failed_runs // 0' <<<"$rfjson")"
      recover_final_fail_stage_top="$(jq -r '.final_fail_stage_top // ""' <<<"$rfjson")"
      recover_final_fail_stage_top_count="$(jq -r '.final_fail_stage_top_count // 0' <<<"$rfjson")"
      while IFS=$'\t' read -r c1 c2 c3 c4 c5 c6 c7 c8 c9 c10 c11; do
        [[ -n "$c1" ]] || continue
        append_fail_stage_by_point "$c1" "$c2" "$c3" "$c4" "$c5" "$c6" "$c7" "$c8" "$c9" "$c10" "$c11"
      done < <(emit_fail_stage_rows "$group_name" "$group_order" "$point_order" "$n" "$t" "$point_key" recover "$point_dir" "$rfjson")
    fi
  fi

  if [[ "$ENABLE_TRACE" == "1" ]]; then
    local trace_summary="$point_dir/trace/summary.csv"
    local trace_attempts="$point_dir/trace/attempts.csv"
    if [[ -f "$trace_summary" ]]; then
      local tjson
      tjson="$(summarize_csv "$trace_summary" trace)"
      trace_runs="$(jq -r '.runs // 0' <<<"$tjson")"
      trace_pass_count="$(jq -r '.pass_count // 0' <<<"$tjson")"
      trace_pass_rate="$(jq -r '.pass_rate // 0' <<<"$tjson")"
      avg_trace_setup_ms="$(jq -r '.avg_setup_ms // 0' <<<"$tjson")"
      avg_load_box_ms="$(jq -r '.avg_load_box_ms // 0' <<<"$tjson")"
      avg_oracle_probe_ms="$(jq -r '.avg_oracle_probe_ms // 0' <<<"$tjson")"
      avg_trace_run_ms="$(jq -r '.avg_trace_run_ms // 0' <<<"$tjson")"
      avg_trace_verify_ms="$(jq -r '.avg_trace_verify_ms // 0' <<<"$tjson")"
      avg_publish_ms="$(jq -r '.avg_publish_ms // 0' <<<"$tjson")"
      avg_trace_query_count="$(jq -r '.avg_trace_query_count // 0' <<<"$tjson")"
      avg_accused_set_size="$(jq -r '.avg_accused_set_size // 0' <<<"$tjson")"
      if [[ "$trace_pass_count" == "$trace_runs" && "$trace_runs" != "0" ]]; then trace_status="pass"; else trace_status="partial"; fi
    else
      trace_status="fail"
    fi
    if [[ -f "$trace_attempts" || -f "$trace_summary" ]]; then
      local tfjson
      tfjson="$(summarize_fail_stages "$trace_attempts" "$trace_summary")"
      trace_failed_attempts="$(jq -r '.attempt_failed_total // 0' <<<"$tfjson")"
      trace_attempt_fail_stage_top="$(jq -r '.attempt_fail_stage_top // ""' <<<"$tfjson")"
      trace_attempt_fail_stage_top_count="$(jq -r '.attempt_fail_stage_top_count // 0' <<<"$tfjson")"
      trace_final_failed_runs="$(jq -r '.final_failed_runs // 0' <<<"$tfjson")"
      trace_final_fail_stage_top="$(jq -r '.final_fail_stage_top // ""' <<<"$tfjson")"
      trace_final_fail_stage_top_count="$(jq -r '.final_fail_stage_top_count // 0' <<<"$tfjson")"
      while IFS=$'\t' read -r c1 c2 c3 c4 c5 c6 c7 c8 c9 c10 c11; do
        [[ -n "$c1" ]] || continue
        append_fail_stage_by_point "$c1" "$c2" "$c3" "$c4" "$c5" "$c6" "$c7" "$c8" "$c9" "$c10" "$c11"
      done < <(emit_fail_stage_rows "$group_name" "$group_order" "$point_order" "$n" "$t" "$point_key" trace "$point_dir" "$tfjson")
    fi
  fi

  append_agg \
    "$group_name" "$group_order" "$point_order" "$n" "$t" "$point_key" "$leaked_indexes" "$challenge_count" "$TRACE_MAX_QUERIES" \
    "$recover_status" "$recover_runs" "$recover_pass_count" "$recover_pass_rate" "$avg_recover_setup_ms" "$avg_recover_ms" "$avg_rotate_ms" "$avg_share_count_used" \
    "$recover_failed_attempts" "$recover_attempt_fail_stage_top" "$recover_attempt_fail_stage_top_count" "$recover_final_failed_runs" "$recover_final_fail_stage_top" "$recover_final_fail_stage_top_count" \
    "$trace_status" "$trace_runs" "$trace_pass_count" "$trace_pass_rate" "$avg_trace_setup_ms" "$avg_load_box_ms" "$avg_oracle_probe_ms" "$avg_trace_run_ms" "$avg_trace_verify_ms" "$avg_publish_ms" "$avg_trace_query_count" "$avg_accused_set_size" \
    "$trace_failed_attempts" "$trace_attempt_fail_stage_top" "$trace_attempt_fail_stage_top_count" "$trace_final_failed_runs" "$trace_final_fail_stage_top" "$trace_final_fail_stage_top_count" \
    "$point_dir" "$note"
}

run_group(){
  local group_name="$1" group_order="$2" pair_list="$3" action="${4:-summarize}"
  local point_order=1
  IFS=',' read -r -a pairs <<< "$pair_list"
  local pair n t
  for pair in "${pairs[@]}"; do
    pair="${pair// /}"
    [[ -n "$pair" ]] || continue
    n="${pair%%:*}"
    t="${pair##*:}"
    [[ "$n" =~ ^[0-9]+$ && "$t" =~ ^[0-9]+$ ]] || die "bad pair: $pair"
    run_point "$group_name" "$group_order" "$point_order" "$n" "$t" "$action"
    point_order=$((point_order + 1))
  done
}

write_group_tables(){
  python3 - "$AGG_CSV" "$OUT_DIR" <<'PY'
import csv, sys, pathlib
agg = pathlib.Path(sys.argv[1])
out = pathlib.Path(sys.argv[2])
rows = list(csv.DictReader(agg.open(newline='', encoding='utf-8')))
if not rows:
    raise SystemExit(0)
by_group = {}
for r in rows:
    by_group.setdefault(r['group_name'], []).append(r)
for group, items in by_group.items():
    items.sort(key=lambda x: (int(x['group_order']), int(x['point_order'])))
    path = out / f'{group}.csv'
    with path.open('w', newline='', encoding='utf-8') as f:
        w = csv.DictWriter(f, fieldnames=rows[0].keys())
        w.writeheader()
        w.writerows(items)
PY
}

write_fail_stage_overall(){
  python3 - "$FAIL_STAGE_BY_POINT_CSV" "$FAIL_STAGE_OVERALL_CSV" <<'PY'
import csv, sys
from collections import Counter
src, dst = sys.argv[1], sys.argv[2]
with open(src, newline='', encoding='utf-8') as f:
    rows = list(csv.DictReader(f))
counter = Counter()
for r in rows:
    key = (r['kind'], r['scope'], r['fail_stage'])
    try:
        counter[key] += int(r['count'])
    except ValueError:
        pass
with open(dst, 'w', newline='', encoding='utf-8') as f:
    w = csv.writer(f)
    w.writerow(['kind', 'scope', 'fail_stage', 'count'])
    for (kind, scope, fail_stage), count in sorted(counter.items(), key=lambda kv: (kv[0][0], kv[0][1], -kv[1], kv[0][2])):
        w.writerow([kind, scope, fail_stage, count])
PY
}

print_fail_stage_digest(){
  python3 - "$FAIL_STAGE_OVERALL_CSV" <<'PY'
import csv, sys
rows = list(csv.DictReader(open(sys.argv[1], newline='', encoding='utf-8')))
if not rows:
    print('no fail_stage records')
    raise SystemExit(0)
for kind in ['recover', 'trace']:
    for scope in ['attempt', 'final_run']:
        items = [r for r in rows if r['kind'] == kind and r['scope'] == scope]
        if not items:
            continue
        items.sort(key=lambda r: (-int(r['count']), r['fail_stage']))
        top = ', '.join(f"{r['fail_stage']}={r['count']}" for r in items[:5])
        print(f"{kind}/{scope}: {top}")
PY
}

main(){
  check_health
  local count
  count="$(available_committee_count)"
  [[ "$count" -ge 8 ]] || die "this experiment needs at least 8 committee nodes; have $count"
  write_headers

  if [[ "$ENABLE_RECOVER" == "1" ]]; then
    log "phase: recover"
    run_group fixed_t4 1 "$GROUP_FIXED_T4" recover
    run_group fixed_n6 2 "$GROUP_FIXED_N6" recover
    run_group selected_points 3 "$GROUP_SELECTED" recover
  fi

  if [[ "$ENABLE_TRACE" == "1" ]]; then
    log "phase: trace"
    run_group fixed_t4 1 "$GROUP_FIXED_T4" trace
    run_group fixed_n6 2 "$GROUP_FIXED_N6" trace
    run_group selected_points 3 "$GROUP_SELECTED" trace
  fi

  log "phase: summarize"
  run_group fixed_t4 1 "$GROUP_FIXED_T4" summarize
  run_group fixed_n6 2 "$GROUP_FIXED_N6" summarize
  run_group selected_points 3 "$GROUP_SELECTED" summarize

  write_group_tables
  write_fail_stage_overall
  log "done: out=$OUT_DIR"
  log "aggregate: $AGG_CSV"
  log "fail_stage_by_point: $FAIL_STAGE_BY_POINT_CSV"
  log "fail_stage_overall: $FAIL_STAGE_OVERALL_CSV"
  while IFS= read -r line; do
    log "$line"
  done < <(print_fail_stage_digest)
}

main "$@"
