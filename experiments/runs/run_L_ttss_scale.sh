#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

ROOT="${ROOT:-${PROJECT_ROOT:-$(cd "$SCRIPT_DIR/../.." && pwd)}}"
OUT_BASE="${OUT_BASE:-$ROOT/results}"
RUN_J_SCRIPT="${RUN_J_SCRIPT:-$ROOT/experiments/runs/run_J_ttss_recover_batch.sh}"
RUN_K_SCRIPT="${RUN_K_SCRIPT:-$ROOT/experiments/runs/run_K_ttss_trace_batch.sh}"
RUN_I_SCRIPT="${RUN_I_SCRIPT:-$ROOT/experiments/runs/run_I_zk_scale.sh}"
BIN="${BIN:-$ROOT/build/did_demo_zk}"
BB="${BB:-http://127.0.0.1:3000}"
PIRATE="${PIRATE:-http://127.0.0.1:4000}"
TOKEN="${TOKEN:-demo-token}"
COMMITTEE_URLS="${COMMITTEE_URLS:-http://127.0.0.1:8001,http://127.0.0.1:8002,http://127.0.0.1:8003,http://127.0.0.1:8004,http://127.0.0.1:8005,http://127.0.0.1:8006}"
PROJECT_ROOT="${PROJECT_ROOT:-$ROOT}"
ENABLE_RECOVER="${ENABLE_RECOVER:-1}"
ENABLE_TRACE="${ENABLE_TRACE:-1}"
ENABLE_ZK_DEPTH="${ENABLE_ZK_DEPTH:-0}"
RECOVER_RUNS="${RECOVER_RUNS:-3}"
TRACE_RUNS="${TRACE_RUNS:-3}"
TRACE_MAX_RETRIES="${TRACE_MAX_RETRIES:-2}"
RECOVER_MAX_RETRIES="${RECOVER_MAX_RETRIES:-2}"
TRACE_ENABLE_MAIN_CROSSCHECK="${TRACE_ENABLE_MAIN_CROSSCHECK:-0}"
TRACE_STRICT_FULL_SET="${TRACE_STRICT_FULL_SET:-1}"
TRACE_LEAKED_INDEXES="${TRACE_LEAKED_INDEXES:-2,4}"
TRACE_DELTA="${TRACE_DELTA:-0.000001}"
TRACE_MAX_QUERIES_LIST="${TRACE_MAX_QUERIES_LIST:-3,5}"
TRACE_CHALLENGE_COUNT_LIST="${TRACE_CHALLENGE_COUNT_LIST:-1}"
PAIR_LIST="${PAIR_LIST:-auto}"
ZK_DEPTH_LIST="${ZK_DEPTH_LIST:-16 20 24 28}"
ZK_RUNS_PER_DEPTH="${ZK_RUNS_PER_DEPTH:-3}"
ZK_BB_EACH="${ZK_BB_EACH:-0}"
TIMEOUT_MS="${TIMEOUT_MS:-120000}"
REGISTER_WAIT_MS="${REGISTER_WAIT_MS:-5000}"
PATH_WAIT_MS="${PATH_WAIT_MS:-5000}"
ROOT_WAIT_MS="${ROOT_WAIT_MS:-15000}"
ROOT_POLL_MS="${ROOT_POLL_MS:-500}"

TS="$(date +%Y%m%d_%H%M%S)"
OUT_DIR="${OUT_DIR:-$OUT_BASE/${TS}_L_ttss_scale}"
mkdir -p "$OUT_DIR"
RUN_LOG="$OUT_DIR/run.log"
AGG_CSV="$OUT_DIR/aggregate.csv"
CONFIG_JSON="$OUT_DIR/config.json"

log(){ echo "[run_L_scale] $*" | tee -a "$RUN_LOG"; }
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

default_pair_list(){
  local count="$1"
  if [[ "$count" -ge 9 ]]; then
    echo "5:3,7:4,9:5"
  elif [[ "$count" -ge 7 ]]; then
    echo "5:3,7:4"
  elif [[ "$count" -ge 6 ]]; then
    echo "5:3,6:4"
  elif [[ "$count" -ge 5 ]]; then
    echo "5:3"
  else
    die "need at least 5 committee nodes for default scale plan; have $count"
  fi
}

write_agg_header(){
  cat > "$AGG_CSV" <<'CSV'
kind,status,n,t,challenge_count,max_queries,depth,runs,pass_count,pass_rate,avg_setup_ms,avg_recover_ms,avg_rotate_ms,avg_load_box_ms,avg_oracle_probe_ms,avg_trace_run_ms,avg_trace_verify_ms,avg_publish_ms,avg_trace_query_count,avg_accused_set_size,out_dir,err
CSV
}

append_agg(){
  local kind="$1" status="$2" n="$3" t="$4" challenge_count="$5" max_queries="$6" depth="$7" runs="$8" pass_count="$9" pass_rate="${10}" avg_setup_ms="${11}" avg_recover_ms="${12}" avg_rotate_ms="${13}" avg_load_box_ms="${14}" avg_oracle_probe_ms="${15}" avg_trace_run_ms="${16}" avg_trace_verify_ms="${17}" avg_publish_ms="${18}" avg_trace_query_count="${19}" avg_accused_set_size="${20}" out_dir="${21}" err="${22}"
  python3 - "$AGG_CSV" "$kind" "$status" "$n" "$t" "$challenge_count" "$max_queries" "$depth" "$runs" "$pass_count" "$pass_rate" "$avg_setup_ms" "$avg_recover_ms" "$avg_rotate_ms" "$avg_load_box_ms" "$avg_oracle_probe_ms" "$avg_trace_run_ms" "$avg_trace_verify_ms" "$avg_publish_ms" "$avg_trace_query_count" "$avg_accused_set_size" "$out_dir" "$err" <<'PY'
import csv,sys
with open(sys.argv[1], 'a', newline='') as f:
    csv.writer(f).writerow(sys.argv[2:])
PY
}

summarize_csv(){
  python3 - "$1" "$2" <<'PY'
import csv, json, sys
from statistics import mean
path = sys.argv[1]
kind = sys.argv[2]
rows = []
with open(path, newline='', encoding='utf-8') as f:
    reader = csv.DictReader(f)
    rows = list(reader)
if not rows:
    print(json.dumps({"runs":0,"pass_count":0,"pass_rate":0.0}))
    raise SystemExit(0)
pass_count = sum(1 for r in rows if str(r.get('pass','0')) == '1')
res = {
    'runs': len(rows),
    'pass_count': pass_count,
    'pass_rate': round(pass_count / len(rows), 6),
}
def avg_of(key):
    vals=[]
    for r in rows:
        v=str(r.get(key,'')).strip()
        if v == '':
            continue
        try:
            vals.append(float(v))
        except ValueError:
            pass
    return round(mean(vals), 3) if vals else 0.0
if kind == 'recover':
    for key in ['setup_ms','recover_ms','rotate_ms']:
        res['avg_' + key] = avg_of(key)
else:
    for key in ['setup_ms','load_box_ms','oracle_probe_ms','trace_run_ms','trace_verify_ms','publish_ms','trace_query_count','accused_set_size']:
        res['avg_' + key] = avg_of(key)
print(json.dumps(res, ensure_ascii=False))
PY
}

run_recover_point(){
  local n="$1" t="$2" point_dir="$3"
  local subset_urls summary_json status err
  subset_urls="$(subset_committee_urls "$n")" || { append_agg recover fail "$n" "$t" "" "" "" "$RECOVER_RUNS" 0 0 0 0 0 0 0 0 0 0 0 "$point_dir" "subset_committee_urls failed"; return 1; }
  local job_dir="$point_dir/recover"
  mkdir -p "$job_dir"
  log "recover scale point n=$n t=$t runs=$RECOVER_RUNS out=$job_dir"
  if ROOT="$ROOT" BIN="$BIN" OUT_DIR="$job_dir" OUT_BASE="$OUT_BASE" BB="$BB" TOKEN="$TOKEN" COMMITTEE_URLS="$subset_urls" RUNS="$RECOVER_RUNS" MAX_RETRIES="$RECOVER_MAX_RETRIES" TTSS_N="$n" TTSS_T="$t" PROJECT_ROOT="$PROJECT_ROOT" TIMEOUT_MS="$TIMEOUT_MS" REGISTER_WAIT_MS="$REGISTER_WAIT_MS" PATH_WAIT_MS="$PATH_WAIT_MS" ROOT_WAIT_MS="$ROOT_WAIT_MS" ROOT_POLL_MS="$ROOT_POLL_MS" bash "$RUN_J_SCRIPT" >>"$RUN_LOG" 2>&1; then
    status="pass"
    err=""
  else
    status="fail"
    err="recover batch failed"
  fi
  summary_json="$(summarize_csv "$job_dir/summary.csv" recover 2>/dev/null || echo '{"runs":0,"pass_count":0,"pass_rate":0.0}')"
  append_agg recover "$status" "$n" "$t" "" "" "" "$(jq -r '.runs // 0' <<<"$summary_json")" "$(jq -r '.pass_count // 0' <<<"$summary_json")" "$(jq -r '.pass_rate // 0' <<<"$summary_json")" "$(jq -r '.avg_setup_ms // 0' <<<"$summary_json")" "$(jq -r '.avg_recover_ms // 0' <<<"$summary_json")" "$(jq -r '.avg_rotate_ms // 0' <<<"$summary_json")" 0 0 0 0 0 0 0 "$job_dir" "$err"
  [[ "$status" == "pass" ]]
}

run_trace_point(){
  local n="$1" t="$2" challenge_count="$3" max_queries="$4" point_dir="$5"
  local subset_urls summary_json status err
  subset_urls="$(subset_committee_urls "$n")" || { append_agg trace fail "$n" "$t" "$challenge_count" "$max_queries" "" "$TRACE_RUNS" 0 0 0 0 0 0 0 0 0 0 0 0 "$point_dir" "subset_committee_urls failed"; return 1; }
  local job_dir="$point_dir/trace_q${max_queries}_c${challenge_count}"
  mkdir -p "$job_dir"
  log "trace scale point n=$n t=$t challenge_count=$challenge_count max_queries=$max_queries runs=$TRACE_RUNS out=$job_dir"
  if ROOT="$ROOT" BIN="$BIN" OUT_DIR="$job_dir" OUT_BASE="$OUT_BASE" BB="$BB" PIRATE="$PIRATE" TOKEN="$TOKEN" COMMITTEE_URLS="$subset_urls" RUNS="$TRACE_RUNS" MAX_RETRIES="$TRACE_MAX_RETRIES" TTSS_N="$n" TTSS_T="$t" PROJECT_ROOT="$PROJECT_ROOT" CHALLENGE_COUNT="$challenge_count" MAX_QUERIES="$max_queries" LEAKED_INDEXES="$TRACE_LEAKED_INDEXES" ENABLE_MAIN_CROSSCHECK="$TRACE_ENABLE_MAIN_CROSSCHECK" STRICT_FULL_SET="$TRACE_STRICT_FULL_SET" DELTA="$TRACE_DELTA" TIMEOUT_MS="$TIMEOUT_MS" REGISTER_WAIT_MS="$REGISTER_WAIT_MS" PATH_WAIT_MS="$PATH_WAIT_MS" ROOT_WAIT_MS="$ROOT_WAIT_MS" ROOT_POLL_MS="$ROOT_POLL_MS" bash "$RUN_K_SCRIPT" >>"$RUN_LOG" 2>&1; then
    status="pass"
    err=""
  else
    status="fail"
    err="trace batch failed"
  fi
  summary_json="$(summarize_csv "$job_dir/summary.csv" trace 2>/dev/null || echo '{"runs":0,"pass_count":0,"pass_rate":0.0}')"
  append_agg trace "$status" "$n" "$t" "$challenge_count" "$max_queries" "" "$(jq -r '.runs // 0' <<<"$summary_json")" "$(jq -r '.pass_count // 0' <<<"$summary_json")" "$(jq -r '.pass_rate // 0' <<<"$summary_json")" "$(jq -r '.avg_setup_ms // 0' <<<"$summary_json")" 0 0 "$(jq -r '.avg_load_box_ms // 0' <<<"$summary_json")" "$(jq -r '.avg_oracle_probe_ms // 0' <<<"$summary_json")" "$(jq -r '.avg_trace_run_ms // 0' <<<"$summary_json")" "$(jq -r '.avg_trace_verify_ms // 0' <<<"$summary_json")" "$(jq -r '.avg_publish_ms // 0' <<<"$summary_json")" "$(jq -r '.avg_trace_query_count // 0' <<<"$summary_json")" "$(jq -r '.avg_accused_set_size // 0' <<<"$summary_json")" "$job_dir" "$err"
  [[ "$status" == "pass" ]]
}

run_zk_depth_scale(){
  local job_dir="$1/zk_depth"
  mkdir -p "$job_dir"
  [[ -f "$RUN_I_SCRIPT" ]] || { append_agg zk_depth fail "" "" "" "" "all" 0 0 0 0 0 0 0 0 0 0 0 0 0 "$job_dir" "missing run_I_zk_scale.sh"; return 1; }
  log "zk depth scale depths=$ZK_DEPTH_LIST runs_per_depth=$ZK_RUNS_PER_DEPTH out=$job_dir"
  local status="pass" err=""
  if PROJECT_ROOT="$PROJECT_ROOT" OUT="$job_dir" DEPTHS="$ZK_DEPTH_LIST" RUNS_PER_DEPTH="$ZK_RUNS_PER_DEPTH" BB_EACH="$ZK_BB_EACH" bash "$RUN_I_SCRIPT" >>"$RUN_LOG" 2>&1; then
    :
  else
    status="fail"
    err="run_I_zk_scale failed"
  fi
  local depth
  for depth in $ZK_DEPTH_LIST; do
    local csv_path="$job_dir/depth_${depth}.csv"
    if [[ -f "$csv_path" ]]; then
      local stats
      stats="$(python3 - "$csv_path" <<'PY'
import csv,json,sys
from statistics import mean
rows=list(csv.DictReader(open(sys.argv[1], newline='', encoding='utf-8')))
if not rows:
    print(json.dumps({'runs':0,'pass_count':0,'pass_rate':0.0,'avg_prove_ms':0.0,'avg_verify_ms':0.0}))
    raise SystemExit(0)
pass_count=sum(1 for r in rows if str(r.get('ok','0'))=='1')
prove=[float(r['prove_ms']) for r in rows if r.get('prove_ms')]
verify=[float(r['verify_ms']) for r in rows if r.get('verify_ms')]
print(json.dumps({'runs':len(rows),'pass_count':pass_count,'pass_rate':round(pass_count/len(rows),6),'avg_prove_ms':round(mean(prove),3) if prove else 0.0,'avg_verify_ms':round(mean(verify),3) if verify else 0.0}))
PY
)"
      append_agg zk_depth "$status" "" "" "" "" "$depth" "$(jq -r '.runs // 0' <<<"$stats")" "$(jq -r '.pass_count // 0' <<<"$stats")" "$(jq -r '.pass_rate // 0' <<<"$stats")" 0 0 0 "$(jq -r '.avg_prove_ms // 0' <<<"$stats")" 0 0 "$(jq -r '.avg_verify_ms // 0' <<<"$stats")" 0 0 0 "$csv_path" "$err"
    else
      append_agg zk_depth fail "" "" "" "" "$depth" 0 0 0 0 0 0 0 0 0 0 0 0 0 "$job_dir" "missing $csv_path"
      status="fail"
    fi
  done
  [[ "$status" == "pass" ]]
}

check_health
write_agg_header

avail_count="$(available_committee_count)"
if [[ "$PAIR_LIST" == "auto" ]]; then
  PAIR_LIST="$(default_pair_list "$avail_count")"
fi

jq -nc \
  --arg root "$ROOT" \
  --arg out_dir "$OUT_DIR" \
  --arg run_j_script "$RUN_J_SCRIPT" \
  --arg run_k_script "$RUN_K_SCRIPT" \
  --arg run_i_script "$RUN_I_SCRIPT" \
  --arg bb "$BB" \
  --arg pirate "$PIRATE" \
  --arg token "$TOKEN" \
  --arg committee_urls "$COMMITTEE_URLS" \
  --arg pair_list "$PAIR_LIST" \
  --arg trace_max_queries_list "$TRACE_MAX_QUERIES_LIST" \
  --arg trace_challenge_count_list "$TRACE_CHALLENGE_COUNT_LIST" \
  --arg zk_depth_list "$ZK_DEPTH_LIST" \
  --argjson enable_recover "$ENABLE_RECOVER" \
  --argjson enable_trace "$ENABLE_TRACE" \
  --argjson enable_zk_depth "$ENABLE_ZK_DEPTH" \
  --argjson recover_runs "$RECOVER_RUNS" \
  --argjson trace_runs "$TRACE_RUNS" \
  '{root:$root,out_dir:$out_dir,run_j_script:$run_j_script,run_k_script:$run_k_script,run_i_script:$run_i_script,bb:$bb,pirate:$pirate,token:$token,committee_urls:$committee_urls,pair_list:$pair_list,trace_max_queries_list:$trace_max_queries_list,trace_challenge_count_list:$trace_challenge_count_list,zk_depth_list:$zk_depth_list,enable_recover:$enable_recover,enable_trace:$enable_trace,enable_zk_depth:$enable_zk_depth,recover_runs:$recover_runs,trace_runs:$trace_runs}' > "$CONFIG_JSON"

overall_ok=1
IFS=',' read -r -a pair_arr <<< "$PAIR_LIST"
IFS=',' read -r -a query_arr <<< "$TRACE_MAX_QUERIES_LIST"
IFS=',' read -r -a challenge_arr <<< "$TRACE_CHALLENGE_COUNT_LIST"

for pair in "${pair_arr[@]}"; do
  [[ -n "$pair" ]] || continue
  n="${pair%%:*}"
  t="${pair##*:}"
  [[ "$n" =~ ^[0-9]+$ && "$t" =~ ^[0-9]+$ ]] || die "bad pair entry: $pair"
  if [[ "$t" -gt "$n" ]]; then
    append_agg plan fail "$n" "$t" "" "" "" 0 0 0 0 0 0 0 0 0 0 0 0 0 "$OUT_DIR" "t > n"
    overall_ok=0
    continue
  fi
  if [[ "$n" -gt "$avail_count" ]]; then
    append_agg plan fail "$n" "$t" "" "" "" 0 0 0 0 0 0 0 0 0 0 0 0 0 "$OUT_DIR" "n exceeds available committee count=$avail_count"
    overall_ok=0
    continue
  fi
  point_dir="$OUT_DIR/n${n}_t${t}"
  mkdir -p "$point_dir"
  if [[ "$ENABLE_RECOVER" == "1" ]]; then
    run_recover_point "$n" "$t" "$point_dir" || overall_ok=0
  fi
  if [[ "$ENABLE_TRACE" == "1" ]]; then
    for q in "${query_arr[@]}"; do
      [[ -n "$q" ]] || continue
      for c in "${challenge_arr[@]}"; do
        [[ -n "$c" ]] || continue
        run_trace_point "$n" "$t" "$c" "$q" "$point_dir" || overall_ok=0
      done
    done
  fi
done

if [[ "$ENABLE_ZK_DEPTH" == "1" ]]; then
  run_zk_depth_scale "$OUT_DIR" || overall_ok=0
fi

log "done: out=$OUT_DIR aggregate=$AGG_CSV"
if [[ "$overall_ok" == "1" ]]; then
  log "PASS"
else
  die "one or more scale points failed"
fi
