#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# run_O_activate_depths_and_test.sh
#
# Purpose:
#   For depths 16..20:
#     1) verify/build per-depth witness generator (auth_membership_cpp/auth_membership)
#     2) activate that depth's auth artifacts into the legacy fixed paths expected by current code
#     3) restart local env with TREE_DEPTH=D
#     4) run anonymous-auth test script
#     5) collect logs, rc, and compact error info
#
# This version is synchronized with the relaxed wait parameters used by run_G_zk_auth.sh.

ROOT="${ROOT:-${PROJECT_ROOT:-$(cd "$SCRIPT_DIR/../.." && pwd)}}"
PROJECT_ROOT="$ROOT"
SCRIPTS_DIR="${SCRIPTS_DIR:-$ROOT/scripts}"
RESULTS_DIR="${RESULTS_DIR:-$ROOT/results}"
RUN_DIR="${RUN_DIR:-$ROOT/run}"

START_STOP_SCRIPT="${START_STOP_SCRIPT:-$SCRIPTS_DIR/start_stop.sh}"
RUN_G_SCRIPT="${RUN_G_SCRIPT:-$ROOT/experiments/runs/run_G_zk_auth.sh}"
ENV_FILE="${ENV_FILE:-$RUN_DIR/ttss_phase5_env.sh}"

DEPTH_LIST="${DEPTH_LIST:-16,17,18,19,20}"

# Synchronized wait / polling defaults for run_G and restart flow.
TIMEOUT_MS="${TIMEOUT_MS:-30000}"
REGISTER_WAIT_MS="${REGISTER_WAIT_MS:-120000}"
PATH_WAIT_MS="${PATH_WAIT_MS:-120000}"
ROOT_WAIT_MS="${ROOT_WAIT_MS:-60000}"
ROOT_POLL_MS="${ROOT_POLL_MS:-300}"

# Optional overrides passed to run_G.
RUN_G_ARGS="${RUN_G_ARGS:-}"
RUNS_BB1="${RUNS_BB1:-50}"
RUNS_BB0="${RUNS_BB0:-50}"

CONTINUE_ON_ERROR="${CONTINUE_ON_ERROR:-1}"

need_cmd() { command -v "$1" >/dev/null 2>&1 || { echo "[run_O_depth_test] FATAL: missing command: $1" >&2; exit 1; }; }
log() { echo "[run_O_depth_test] $*"; }
die() { echo "[run_O_depth_test] FATAL: $*" >&2; exit 1; }

need_cmd bash
need_cmd cp
need_cmd rm
need_cmd mkdir
need_cmd sed
need_cmd awk
need_cmd tail
need_cmd make
need_cmd curl

[[ -d "$ROOT" ]] || die "ROOT not found: $ROOT"
[[ -f "$START_STOP_SCRIPT" ]] || die "start/stop script not found: $START_STOP_SCRIPT"
[[ -f "$RUN_G_SCRIPT" ]] || die "run_G script not found: $RUN_G_SCRIPT"

TS="$(date +%Y%m%d_%H%M%S)"
OUTDIR="${OUTDIR:-$RESULTS_DIR/run_O_activate_depths_and_test_$TS}"
mkdir -p "$OUTDIR"

RESULTS_CSV="$OUTDIR/results.csv"
ERRORS_CSV="$OUTDIR/errors.csv"

cat > "$RESULTS_CSV" <<'CSV'
depth,cpp_ready,zkey_ready,vk_ready,activate_ok,restart_ok,auth_rc,auth_ok,note,depth_dir
CSV

cat > "$ERRORS_CSV" <<'CSV'
depth,stage,rc,error_message,log_file
CSV

trim() {
  local x="$1"
  x="${x#"${x%%[![:space:]]*}"}"
  x="${x%"${x##*[![:space:]]}"}"
  printf '%s' "$x"
}

csv_to_lines() {
  local csv="$1"
  echo "$csv" | tr ',' '\n' | sed '/^[[:space:]]*$/d'
}

append_error() {
  local depth="$1"
  local stage="$2"
  local rc="$3"
  local msg="$4"
  local file="$5"
  msg="${msg//$'\n'/ }"
  msg="${msg//\"/\"\"}"
  file="${file//\"/\"\"}"
  printf '%s,%s,%s,"%s","%s"\n' "$depth" "$stage" "$rc" "$msg" "$file" >> "$ERRORS_CSV"
}

append_result() {
  local depth="$1"
  local cpp_ready="$2"
  local zkey_ready="$3"
  local vk_ready="$4"
  local activate_ok="$5"
  local restart_ok="$6"
  local auth_rc="$7"
  local auth_ok="$8"
  local note="$9"
  local depth_dir="${10}"
  note="${note//\"/\"\"}"
  depth_dir="${depth_dir//\"/\"\"}"
  printf '%s,%s,%s,%s,%s,%s,%s,%s,"%s","%s"\n' \
    "$depth" "$cpp_ready" "$zkey_ready" "$vk_ready" "$activate_ok" "$restart_ok" "$auth_rc" "$auth_ok" "$note" "$depth_dir" >> "$RESULTS_CSV"
}

health_check() {
  local base="${BASE_URL:-http://127.0.0.1:3000}"
  local verify="${VERIFY_HEALTH:-http://127.0.0.1:3400/health}"
  log "health check: bb"
  curl -fsS "$base/health" >/dev/null
  log "health check: verifier"
  curl -fsS "$verify" >/dev/null
}

ensure_cpp() {
  local depth="$1"
  local depth_dir="$2"
  local cpp_bin="$ROOT/zk_build/auth_membership_d${depth}/auth_membership_cpp/auth_membership"
  local cpp_dir="$ROOT/zk_build/auth_membership_d${depth}/auth_membership_cpp"
  local make_log="$depth_dir/build_cpp.log"

  if [[ -x "$cpp_bin" ]]; then
    return 0
  fi

  [[ -d "$cpp_dir" ]] || return 2

  log "depth=$depth build cpp witness generator"
  if make -C "$cpp_dir" >"$make_log" 2>&1; then
    [[ -x "$cpp_bin" ]] && return 0
    return 3
  else
    return 4
  fi
}

activate_depth() {
  local depth="$1"
  local depth_dir="$2"
  local log_file="$depth_dir/activate.log"

  local base="$ROOT/zk_build/auth_membership_d${depth}"
  local cpp_dir="$base/auth_membership_cpp"
  local js_dir="$base/auth_membership_js"
  local zkey="$base/zkey/auth_membership_final.zkey"
  local vk="$base/vk/auth_membership_vk.json"

  {
    echo "[activate] depth=$depth"
    echo "[activate] base=$base"
    test -x "$cpp_dir/auth_membership"
    test -d "$js_dir"
    test -f "$zkey"
    test -f "$vk"

    rm -rf "$ROOT/zk_build/auth_membership"
    mkdir -p "$ROOT/zk_build/auth_membership"
    mkdir -p "$ROOT/zk_build/zkey"
    mkdir -p "$ROOT/zk_build/vk"

    cp -r "$cpp_dir" "$ROOT/zk_build/auth_membership/"
    cp -r "$js_dir"  "$ROOT/zk_build/auth_membership/"

    cp "$zkey" "$ROOT/zk_build/auth_membership/auth_membership_final.zkey"
    cp "$zkey" "$ROOT/zk_build/zkey/auth_membership_final.zkey"

    cp "$vk" "$ROOT/zk_build/vk/auth_membership_vk.json"
    cp "$vk" "$ROOT/zk_build/auth_membership/verification_key.json"

    echo "[activate] done"
  } >"$log_file" 2>&1
}

restart_for_depth() {
  local depth="$1"
  local depth_dir="$2"
  local log_file="$depth_dir/restart.log"

  log "restart environment TREE_DEPTH=$depth"
  if env \
      PROJECT_ROOT="$PROJECT_ROOT" \
      TREE_DEPTH="$depth" \
      TIMEOUT_MS="$TIMEOUT_MS" \
      REGISTER_WAIT_MS="$REGISTER_WAIT_MS" \
      PATH_WAIT_MS="$PATH_WAIT_MS" \
      ROOT_WAIT_MS="$ROOT_WAIT_MS" \
      ROOT_POLL_MS="$ROOT_POLL_MS" \
      bash "$START_STOP_SCRIPT" restart >"$log_file" 2>&1; then
    if [[ -f "$ENV_FILE" ]]; then
      # shellcheck disable=SC1090
      source "$ENV_FILE"
    fi
    export PROJECT_ROOT ROOT RESULTS_DIR RUN_DIR SCRIPTS_DIR
    export TREE_DEPTH="$depth"
    export TIMEOUT_MS REGISTER_WAIT_MS PATH_WAIT_MS ROOT_WAIT_MS ROOT_POLL_MS
    health_check >>"$log_file" 2>&1
    return 0
  fi
  return 1
}

run_auth() {
  local depth="$1"
  local depth_dir="$2"
  local stdout_log="$depth_dir/auth.stdout.log"
  local stderr_log="$depth_dir/auth.stderr.log"
  local rc=0

  log "run auth depth=$depth"
  (
    cd "$ROOT"
    env \
      PROJECT_ROOT="$PROJECT_ROOT" \
      ROOT="$ROOT" \
      DEPTH="$depth" \
      TREE_DEPTH="$depth" \
      RUNS_BB1="$RUNS_BB1" \
      RUNS_BB0="$RUNS_BB0" \
      TIMEOUT_MS="$TIMEOUT_MS" \
      REGISTER_WAIT_MS="$REGISTER_WAIT_MS" \
      PATH_WAIT_MS="$PATH_WAIT_MS" \
      ROOT_WAIT_MS="$ROOT_WAIT_MS" \
      ROOT_POLL_MS="$ROOT_POLL_MS" \
      bash "$RUN_G_SCRIPT" $RUN_G_ARGS
  ) >"$stdout_log" 2>"$stderr_log" || rc=$?

  return "$rc"
}

extract_error_message() {
  local stderr_log="$1"
  local stdout_log="$2"

  local msg=""
  if [[ -s "$stderr_log" ]]; then
    msg="$(tail -n 20 "$stderr_log" | tr '\n' ' ' | sed 's/[[:space:]]\+/ /g')"
  fi
  if [[ -z "$msg" && -s "$stdout_log" ]]; then
    msg="$(grep -E 'fatal:|Error |exception|Aborted|fail id=|requested depth=|accepted_but_not_observed_ready_after_register|registerStatus_failed' "$stdout_log" | tail -n 8 | tr '\n' ' ' | sed 's/[[:space:]]\+/ /g' || true)"
  fi
  printf '%s' "$msg"
}

main() {
  local depth
  for depth in $(csv_to_lines "$DEPTH_LIST"); do
    depth="$(trim "$depth")"
    [[ -n "$depth" ]] || continue

    local depth_dir="$OUTDIR/depth_${depth}"
    mkdir -p "$depth_dir"

    local cpp_ready=0
    local zkey_ready=0
    local vk_ready=0
    local activate_ok=0
    local restart_ok=0
    local auth_rc=999
    local auth_ok=0
    local note=""

    local zkey="$ROOT/zk_build/auth_membership_d${depth}/zkey/auth_membership_final.zkey"
    local vk="$ROOT/zk_build/auth_membership_d${depth}/vk/auth_membership_vk.json"

    [[ -f "$zkey" ]] && zkey_ready=1
    [[ -f "$vk" ]] && vk_ready=1

    if ensure_cpp "$depth" "$depth_dir"; then
      cpp_ready=1
    else
      local rc=$?
      local err="cpp_build_missing_or_failed"
      case "$rc" in
        2) err="auth_membership_cpp_dir_missing" ;;
        3) err="cpp_make_finished_but_binary_missing" ;;
        4) err="cpp_make_failed" ;;
      esac
      append_error "$depth" "build_cpp" "$rc" "$err" "$depth_dir/build_cpp.log"
      note="$err"
      append_result "$depth" "$cpp_ready" "$zkey_ready" "$vk_ready" "$activate_ok" "$restart_ok" "$auth_rc" "$auth_ok" "$note" "$depth_dir"
      [[ "$CONTINUE_ON_ERROR" == "1" ]] && continue || die "depth=$depth failed at build_cpp"
    fi

    if activate_depth "$depth" "$depth_dir"; then
      activate_ok=1
    else
      local rc=$?
      local msg
      msg="$(tail -n 20 "$depth_dir/activate.log" | tr '\n' ' ' | sed 's/[[:space:]]\+/ /g')"
      append_error "$depth" "activate" "$rc" "${msg:-activate_failed}" "$depth_dir/activate.log"
      note="activate_failed"
      append_result "$depth" "$cpp_ready" "$zkey_ready" "$vk_ready" "$activate_ok" "$restart_ok" "$auth_rc" "$auth_ok" "$note" "$depth_dir"
      [[ "$CONTINUE_ON_ERROR" == "1" ]] && continue || die "depth=$depth failed at activate"
    fi

    if restart_for_depth "$depth" "$depth_dir"; then
      restart_ok=1
    else
      local rc=$?
      local msg
      msg="$(tail -n 30 "$depth_dir/restart.log" | tr '\n' ' ' | sed 's/[[:space:]]\+/ /g')"
      append_error "$depth" "restart" "$rc" "${msg:-restart_failed}" "$depth_dir/restart.log"
      note="restart_failed"
      append_result "$depth" "$cpp_ready" "$zkey_ready" "$vk_ready" "$activate_ok" "$restart_ok" "$auth_rc" "$auth_ok" "$note" "$depth_dir"
      [[ "$CONTINUE_ON_ERROR" == "1" ]] && continue || die "depth=$depth failed at restart"
    fi

    if run_auth "$depth" "$depth_dir"; then
      auth_rc=0
      auth_ok=1
      note="ok"
    else
      auth_rc=$?
      auth_ok=0
      local msg
      msg="$(extract_error_message "$depth_dir/auth.stderr.log" "$depth_dir/auth.stdout.log")"
      append_error "$depth" "run_auth" "$auth_rc" "${msg:-run_G_failed}" "$depth_dir/auth.stderr.log"
      note="${msg:-run_G_failed}"
    fi

    {
      echo "depth=$depth"
      echo "cpp_ready=$cpp_ready"
      echo "zkey_ready=$zkey_ready"
      echo "vk_ready=$vk_ready"
      echo "activate_ok=$activate_ok"
      echo "restart_ok=$restart_ok"
      echo "auth_rc=$auth_rc"
      echo "auth_ok=$auth_ok"
      echo "TIMEOUT_MS=$TIMEOUT_MS"
      echo "REGISTER_WAIT_MS=$REGISTER_WAIT_MS"
      echo "PATH_WAIT_MS=$PATH_WAIT_MS"
      echo "ROOT_WAIT_MS=$ROOT_WAIT_MS"
      echo "ROOT_POLL_MS=$ROOT_POLL_MS"
      echo "RUNS_BB1=$RUNS_BB1"
      echo "RUNS_BB0=$RUNS_BB0"
      echo "note=$note"
    } >"$depth_dir/result_meta.txt"

    append_result "$depth" "$cpp_ready" "$zkey_ready" "$vk_ready" "$activate_ok" "$restart_ok" "$auth_rc" "$auth_ok" "$note" "$depth_dir"

    if [[ "$auth_ok" != "1" && "$CONTINUE_ON_ERROR" != "1" ]]; then
      die "depth=$depth failed at run_auth"
    fi
  done

  log "done"
  log "results: $RESULTS_CSV"
  log "errors : $ERRORS_CSV"
}

main "$@"
