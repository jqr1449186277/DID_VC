#!/usr/bin/env bash
set -euo pipefail

CMD="${1:-up}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="${PROJECT_ROOT:-$(cd "$SCRIPT_DIR/.." && pwd)}"
export PROJECT_ROOT

# shellcheck source=scripts/dev_config.sh
source "$SCRIPT_DIR/dev_config.sh"

mkdir -p "$RUN_DIR" "$LOG_DIR" "$RESULTS_DIR"

# shellcheck source=scripts/dev_common.sh
source "$SCRIPTS_DIR/dev_common.sh"
dev_require_base_commands


start_hardhat() {
  if rpc_ready; then
    log "hardhat already healthy"
    return 0
  fi
  stop_pid_file "$NODE_PID_FILE" "hardhat-node"
  log "starting hardhat node"
  nohup bash -lc "cd '$HARDHAT_DIR' && npx hardhat node" > "$LOG_DIR/hardhat-node.log" 2>&1 &
  echo "$!" > "$NODE_PID_FILE"
  wait_rpc "$WAIT_SEC" || die "hardhat node not ready; see $LOG_DIR/hardhat-node.log"
}

extract_contract() {
  local deploy_log="$1"
  sed -n 's/^DIDBulletinBoardZK = \(0x[0-9a-fA-F]\{40\}\)$/\1/p' "$deploy_log" | tail -n 1
}

start_verifier_and_bb() {
  local deploy_log="$LOG_DIR/deploy_zk.log"
  local contract
  local deploy_cmd
  stop_pid_file "$SERVICE_PID_FILE" "bb_service_zk"
  stop_pid_file "$VERIFIER_PID_FILE" "zk_verify_service"

  deploy_cmd="cd '$HARDHAT_DIR' && npx hardhat run '$DEPLOY_SCRIPT' --network localhost"
  if [[ "$HARDHAT_DEPLOY_COMPILE" != "1" ]]; then
    deploy_cmd+=" --no-compile"
    log "deploying DIDBulletinBoardZK (--no-compile)"
  else
    log "deploying DIDBulletinBoardZK (with compile)"
  fi
  if ! bash -lc "$deploy_cmd" > "$deploy_log" 2>&1; then
    tail -n 80 "$deploy_log" || true
    die "deploy_zk failed; see $deploy_log"
  fi
  contract="$(extract_contract "$deploy_log")"
  [[ -n "$contract" ]] || die "failed to parse contract address from $deploy_log"
  export CONTRACT="$contract"

  log "starting zk_verify_service"
  export DIDZK_VERIFY_SERVICE_HOST="$VERIFY_HOST"
  export DIDZK_VERIFY_SERVICE_PORT="$VERIFY_PORT"
  nohup node "$VERIFIER_JS" > "$LOG_DIR/zk_verify_service.log" 2>&1 &
  echo "$!" > "$VERIFIER_PID_FILE"
  wait_http_or_pid "$VERIFY_HEALTH" "$(cat "$VERIFIER_PID_FILE")" "zk_verify_service" "$WAIT_SEC" || die "zk_verify_service not ready; see $LOG_DIR/zk_verify_service.log"

  log "starting bb_service_zk"
  nohup env RPC_URL="$RPC_URL" BASE_URL="$BASE_URL" PORT="$BB_PORT" TREE_DEPTH="$TREE_DEPTH" DIDZK_VERIFY_SERVICE_URL="$VERIFY_URL" DIDZK_VERIFY_SERVICE_HEALTH="$VERIFY_HEALTH" CONTRACT="$CONTRACT" bash -lc "cd '$HARDHAT_DIR' && node '$BB_SERVICE_JS'" > "$LOG_DIR/bb_service_zk.log" 2>&1 &
  echo "$!" > "$SERVICE_PID_FILE"
  wait_http_or_pid "$BASE_URL/health" "$(cat "$SERVICE_PID_FILE")" "bb_service_zk" 180 || die "bb_service_zk not ready; see $LOG_DIR/bb_service_zk.log"
}

start_pirate() {
  if wait_http "$PIRATE_URL/health" 1; then
    log "pirate already healthy"
    return 0
  fi
  stop_pid_file "$PIRATE_PID_FILE" "pirate_box"
  log "starting pirate_box"
  nohup env PORT="$PIRATE_PORT" bash -lc "cd '$HARDHAT_DIR' && node '$PIRATE_JS'" > "$LOG_DIR/pirate_box.log" 2>&1 &
  echo "$!" > "$PIRATE_PID_FILE"
  wait_http_or_pid "$PIRATE_URL/health" "$(cat "$PIRATE_PID_FILE")" "pirate_box" "$WAIT_SEC" || die "pirate not ready; see $LOG_DIR/pirate_box.log"
}

start_committees() {
  local p url pid_file log_file
  for p in $COMMITTEE_PORTS; do
    url="http://127.0.0.1:${p}/health"
    pid_file="${COMMITTEE_PID_PREFIX}${p}.pid"
    log_file="$LOG_DIR/committee_node_${p}.log"
    if wait_http "$url" 1; then
      log "committee already healthy on $p"
      continue
    fi
    stop_pid_file "$pid_file" "committee_node_$p"
    log "starting committee_node on $p"
    nohup "$COMMITTEE_BIN" --port "$p" --token "$TOKEN" --debug_endpoints 1 > "$log_file" 2>&1 &
    echo "$!" > "$pid_file"
    wait_http_or_pid "$url" "$(cat "$pid_file")" "committee_node_$p" "$WAIT_SEC" || die "committee $p not ready; see $log_file"
  done
}

write_env() {
  local committee_urls
  committee_urls="$(committee_urls_csv)"
  cat > "$ENV_FILE" <<EOF_ENV
export PROJECT_ROOT="$PROJECT_ROOT"
export CPP_DIR="$CPP_DIR"
export HARDHAT_DIR="$HARDHAT_DIR"
export BUILD_DIR="$BUILD_DIR"
export MAIN_BIN="$MAIN_BIN"
export COMMITTEE_BIN="$COMMITTEE_BIN"
export BASE_URL="$BASE_URL"
export RPC_URL="$RPC_URL"
export VERIFY_HEALTH="$VERIFY_HEALTH"
export VERIFY_URL="$VERIFY_URL"
export PIRATE="$PIRATE_URL"
export PIRATE_PORT="$PIRATE_PORT"
export PIRATE_URL="$PIRATE_URL"
export TOKEN="$TOKEN"
export TTSS_N="$TTSS_N"
export TTSS_T="$TTSS_T"
export COMMITTEE_PORTS="$COMMITTEE_PORTS"
export COMMITTEE_URLS="$committee_urls"
export TRACER_JS="$TRACER_JS"
export CONTRACT="$CONTRACT"
EOF_ENV
  chmod +x "$ENV_FILE"
}


smoke_ttss_setup() {
  local smoke_id="env_smoke_$(date +%s)"
  local smoke_dir="$RESULTS_DIR/_smoke_phase5_env_${smoke_id}"
  local committee_urls
  local setup_json leaf_json ttss_meta_json
  committee_urls="$(committee_urls_csv)"
  mkdir -p "$smoke_dir"
  setup_json="$smoke_dir/${smoke_id}_ttss_setup/ttss_setup.json"
  leaf_json="$smoke_dir/leaf.json"
  ttss_meta_json="$smoke_dir/ttss_meta.json"

  log "smoke ttss_setup id=$smoke_id"
  if ! "$MAIN_BIN" \
      --ttss_setup \
      --id "$smoke_id" \
      --bb "$BASE_URL" \
      --committee_urls "$committee_urls" \
      --committee_token "$TOKEN" \
      --ttss_n "$TTSS_N" \
      --ttss_t "$TTSS_T" \
      --project_root "$PROJECT_ROOT" \
      --workdir "$smoke_dir" \
      --timeout_ms "$TIMEOUT_MS" \
      --register_wait_ms "$REGISTER_WAIT_MS" \
      --path_wait_ms "$PATH_WAIT_MS" \
      --root_wait_ms "$ROOT_WAIT_MS" \
      --root_poll_ms "$ROOT_POLL_MS" \
      > "$smoke_dir/ttss_setup.out" 2> "$smoke_dir/ttss_setup.err"; then
    echo "{\"ok\":0,\"smokeId\":\"$smoke_id\",\"workdir\":\"$smoke_dir\",\"err\":\"ttss_setup_failed\"}" > "$SMOKE_STATE_FILE"
    log "smoke ttss_setup failed; see $smoke_dir/ttss_setup.err"
    tail -n 80 "$smoke_dir/ttss_setup.err" || true
    return 1
  fi

  [[ -f "$setup_json" ]] || { echo "{\"ok\":0,\"smokeId\":\"$smoke_id\",\"workdir\":\"$smoke_dir\",\"err\":\"missing_setup_json\"}" > "$SMOKE_STATE_FILE"; return 1; }

  python3 - "$BASE_URL" "$smoke_id" "$setup_json" "$leaf_json" "$ttss_meta_json" <<'PY'
import json, sys, time, urllib.request, urllib.parse
base, did, setup_path, leaf_path, meta_path = sys.argv[1:6]
setup = json.load(open(setup_path, 'r', encoding='utf-8'))
last = None
for _ in range(180):
    try:
        with urllib.request.urlopen(f"{base}/leaf?id={urllib.parse.quote(did)}", timeout=3) as r:
            obj = json.loads(r.read().decode('utf-8'))
        last = obj
        if obj.get('ok') == 1 and int(obj.get('active', 0)) == 1:
            json.dump(obj, open(leaf_path, 'w', encoding='utf-8'), ensure_ascii=False, indent=2)
            break
    except Exception as e:
        last = str(e)
    time.sleep(1)
else:
    raise SystemExit(f"leaf_not_ready: {last}")

with urllib.request.urlopen(f"{base}/ttssMeta?id={urllib.parse.quote(did)}", timeout=3) as r:
    meta = json.loads(r.read().decode('utf-8'))
json.dump(meta, open(meta_path, 'w', encoding='utf-8'), ensure_ascii=False, indent=2)

leaf = json.load(open(leaf_path, 'r', encoding='utf-8'))
if meta.get('ok') != 1:
    raise SystemExit(f"ttss_meta_bad: {meta}")

def intish(v):
    return int(v)

setup_id_hash = str(setup.get('idHash', '')).lower()
leaf_id_hash = str(leaf.get('idHash', '')).lower()
if not setup_id_hash or setup_id_hash != leaf_id_hash:
    raise SystemExit(f"idHash_mismatch: setup={setup_id_hash} leaf={leaf_id_hash}")

setup_ver = intish(setup.get('ver', setup.get('version', -1)))
leaf_ver = intish(leaf.get('ver', leaf.get('version', -1)))
if setup_ver != leaf_ver:
    raise SystemExit(f"ver_mismatch: setup={setup_ver} leaf={leaf_ver}")

setup_epoch = intish(setup.get('epoch', -1))
leaf_epoch = intish(leaf.get('epoch', -1))
if setup_epoch != leaf_epoch:
    raise SystemExit(f"epoch_mismatch: setup={setup_epoch} leaf={leaf_epoch}")

meta_ver = intish(meta.get('ver', -1))
meta_epoch = intish(meta.get('epoch', -1))
if meta_ver != setup_ver or meta_epoch != setup_epoch:
    raise SystemExit(f"ttssMeta_version_epoch_mismatch: meta={meta_ver}/{meta_epoch} setup={setup_ver}/{setup_epoch}")

vk = str(meta.get('vkSetHash', '')).lower()
setup_vk = str(setup.get('vkSetHash', '')).lower()
if setup_vk and vk != setup_vk:
    raise SystemExit(f"vkSetHash_mismatch: meta={vk} setup={setup_vk}")

print(json.dumps({
    'ok': 1,
    'smokeId': did,
    'idHash': setup_id_hash,
    'ver': setup_ver,
    'epoch': setup_epoch,
    'vkSetHash': vk,
}, ensure_ascii=False))
PY

  python3 - "$setup_json" "$leaf_json" "$ttss_meta_json" "$SMOKE_STATE_FILE" "$smoke_id" "$smoke_dir" <<'PY'
import json, sys
setup = json.load(open(sys.argv[1], 'r', encoding='utf-8'))
leaf = json.load(open(sys.argv[2], 'r', encoding='utf-8'))
meta = json.load(open(sys.argv[3], 'r', encoding='utf-8'))
out = {
    'ok': 1,
    'smokeId': sys.argv[5],
    'workdir': sys.argv[6],
    'idHash': setup.get('idHash'),
    'ver': setup.get('ver', setup.get('version')),
    'epoch': setup.get('epoch'),
    'vkSetHash': meta.get('vkSetHash'),
    'leafActive': leaf.get('active'),
}
json.dump(out, open(sys.argv[4], 'w', encoding='utf-8'), ensure_ascii=False, indent=2)
PY

  log "smoke ttss_setup ok workdir=$smoke_dir"
}

do_up() {
  assert_paths
  start_hardhat
  start_verifier_and_bb
  start_pirate
  start_committees
  write_env
  if ! health_summary; then
    tail_logs
    die "basic health check failed"
  fi
  if [[ "$SMOKE_TTSS_SETUP" == "1" ]]; then
    if ! smoke_ttss_setup; then
      tail_logs
      die "deep smoke failed; see $(cat "$SMOKE_STATE_FILE" 2>/dev/null || echo "$SMOKE_STATE_FILE")"
    fi
  fi
  log "PASS"
  log "source $ENV_FILE"
  if [[ -f "$SMOKE_STATE_FILE" ]]; then
    log "last smoke: $(cat "$SMOKE_STATE_FILE")"
  fi
}

do_status() {
  assert_paths
  health_summary || true
  if [[ -f "$SMOKE_STATE_FILE" ]]; then
    log "smoke state: $(cat "$SMOKE_STATE_FILE")"
  else
    log "smoke state: none"
  fi
}

do_smoke() {
  assert_paths
  if ! health_summary; then
    tail_logs
    die "basic health not ready for smoke"
  fi
  smoke_ttss_setup
  log "smoke state: $(cat "$SMOKE_STATE_FILE")"
}

do_down() {
  local p
  for p in $COMMITTEE_PORTS; do
    stop_pid_file "${COMMITTEE_PID_PREFIX}${p}.pid" "committee_node_$p"
  done
  stop_pid_file "$PIRATE_PID_FILE" "pirate_box"
  stop_pid_file "$SERVICE_PID_FILE" "bb_service_zk"
  stop_pid_file "$VERIFIER_PID_FILE" "zk_verify_service"
  stop_pid_file "$NODE_PID_FILE" "hardhat-node"
  stop_by_pattern "node .*bb_service_zk\\.js" "bb_service_zk"
  stop_by_pattern "node .*zk_verify_service\\.js" "zk_verify_service"
  stop_by_pattern "node .*pirate_box\\.js" "pirate_box"
  stop_by_pattern "node .*hardhat .*node" "hardhat-node"
  log "stopped managed phase5 processes"
}

case "$CMD" in
  up) do_up ;;
  status) do_status ;;
  smoke) do_smoke ;;
  down) do_down ;;
  restart) do_down || true; do_up ;;
  *) echo "Usage: $0 [up|status|smoke|down|restart]" >&2; exit 2 ;;
esac
