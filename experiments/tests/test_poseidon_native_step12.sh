#!/usr/bin/env bash
set -euo pipefail

ROOT="${ROOT:-${PROJECT_ROOT:-$(cd "$(dirname "$0")/../.." && pwd)}}"
RUN_TAG="$(date +%Y%m%d_%H%M%S)_poseidon_native_step12"
RUN_DIR="$ROOT/results/$RUN_TAG"
BIN="$RUN_DIR/test_poseidon_native_step12"
LOG="$RUN_DIR/run.log"
STATE_BIN="$ROOT/zk_build/state_leaf_check/state_leaf_check_cpp/state_leaf_check"
STATE_SYM="$ROOT/zk_build/state_leaf_check/state_leaf_check.sym"
SNARKJS_CLI="$ROOT/circuits/node_modules/snarkjs/build/cli.cjs"
VECTOR_JSON="$ROOT/zk_inputs/vector_alice.json"
PATH_JSON="$ROOT/zk_inputs/alice_path.json"
INPUT_JSON="$RUN_DIR/state_leaf_check_input.json"
WTNS_BIN="$RUN_DIR/state_leaf_check.wtns"
WTNS_JSON="$RUN_DIR/state_leaf_check_witness.json"
REPORT_JSON="$RUN_DIR/report.json"

mkdir -p "$RUN_DIR"
exec > >(tee -a "$LOG") 2>&1

need_file() {
  [[ -f "$1" ]] || { echo "missing file: $1" >&2; exit 1; }
}

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "missing command: $1" >&2; exit 1; }
}

need_cmd g++
need_cmd jq
need_cmd awk
need_cmd grep
need_cmd node
need_cmd python3
need_file "$ROOT/cpp/merkle_poseidon.cpp"
need_file "$ROOT/cpp/merkle_poseidon.hpp"
need_file "$ROOT/cpp/text_utils.cpp"
need_file "$ROOT/tests/cpp/test_poseidon_native.cpp"
need_file "$STATE_BIN"
need_file "$STATE_SYM"
need_file "$SNARKJS_CLI"
need_file "$VECTOR_JSON"
need_file "$PATH_JSON"

chmod +x "$STATE_BIN"

echo "[step1] compiling native poseidon test binary"
g++ -std=c++17 -O2 -fsanitize=address -fno-omit-frame-pointer -I"$ROOT/cpp" \
  "$ROOT/tests/cpp/test_poseidon_native.cpp" \
  "$ROOT/cpp/merkle_poseidon.cpp" \
  "$ROOT/cpp/text_utils.cpp" \
  -lgmpxx -lgmp \
  -o "$BIN"

echo "[step1] native leaf/cid check against known vector"
SID="$(jq -r '.sid' "$VECTOR_JSON")"
RHO="$(jq -r '.rho' "$VECTOR_JSON")"
PK_NORM="$(jq -r '.pkNormHash' "$VECTOR_JSON")"
PK_REC="$(jq -r '.pkRecHash' "$VECTOR_JSON")"
VER="$(jq -r '.ver' "$VECTOR_JSON")"
ACTIVE="$(jq -r '.active' "$VECTOR_JSON")"
CID_EXPECTED_HEX="$(jq -r '.cidHex' "$VECTOR_JSON")"
LEAF_EXPECTED_HEX="$(jq -r '.leafHex' "$VECTOR_JSON")"
CID_EXPECTED_DEC="$(jq -r '.cidDec' "$VECTOR_JSON")"
LEAF_EXPECTED_DEC="$(jq -r '.leafDec' "$VECTOR_JSON")"

LEAF_OUT="$($BIN leaf "$SID" "$RHO" "$PK_NORM" "$PK_REC" "$VER" "$ACTIVE")"
printf '%s\n' "$LEAF_OUT" > "$RUN_DIR/native_leaf_output.txt"
CID_NATIVE_HEX="$(printf '%s\n' "$LEAF_OUT" | sed -n 's/^cid=//p')"
LEAF_NATIVE_HEX="$(printf '%s\n' "$LEAF_OUT" | sed -n 's/^leaf=//p')"

[[ "$CID_NATIVE_HEX" == "$CID_EXPECTED_HEX" ]] || { echo "cid mismatch: $CID_NATIVE_HEX != $CID_EXPECTED_HEX" >&2; exit 1; }
[[ "$LEAF_NATIVE_HEX" == "$LEAF_EXPECTED_HEX" ]] || { echo "leaf mismatch: $LEAF_NATIVE_HEX != $LEAF_EXPECTED_HEX" >&2; exit 1; }
echo "[step1] native vector match ok"

jq -n \
  --arg sid "$SID" \
  --arg rho "$RHO" \
  --arg pkNormHash "$PK_NORM" \
  --arg pkRecHash "$PK_REC" \
  --arg ver "$VER" \
  --arg active "$ACTIVE" \
  '{sid:$sid, rho:$rho, pkNormHash:$pkNormHash, pkRecHash:$pkRecHash, ver:$ver, active:$active}' > "$INPUT_JSON"

echo "[step1] StateLeafCheck witness alignment"
"$STATE_BIN" "$INPUT_JSON" "$WTNS_BIN"
node "$SNARKJS_CLI" wtns export json "$WTNS_BIN" "$WTNS_JSON"
CID_WIRE_IDX="$(awk -F, '$4=="main.cid" {print $1; exit}' "$STATE_SYM")"
LEAF_WIRE_IDX="$(awk -F, '$4=="main.leaf" {print $1; exit}' "$STATE_SYM")"
[[ -n "$CID_WIRE_IDX" && -n "$LEAF_WIRE_IDX" ]] || { echo "failed to resolve main.cid/main.leaf" >&2; exit 1; }
CID_WITNESS_DEC="$(jq -r --argjson idx "$CID_WIRE_IDX" '.[$idx]' "$WTNS_JSON")"
LEAF_WITNESS_DEC="$(jq -r --argjson idx "$LEAF_WIRE_IDX" '.[$idx]' "$WTNS_JSON")"
[[ "$CID_WITNESS_DEC" == "$CID_EXPECTED_DEC" ]] || { echo "StateLeafCheck cid mismatch: $CID_WITNESS_DEC != $CID_EXPECTED_DEC" >&2; exit 1; }
[[ "$LEAF_WITNESS_DEC" == "$LEAF_EXPECTED_DEC" ]] || { echo "StateLeafCheck leaf mismatch: $LEAF_WITNESS_DEC != $LEAF_EXPECTED_DEC" >&2; exit 1; }
echo "[step1] StateLeafCheck aligned with native cid/leaf"

echo "[step1] root alignment on same path"
python3 - "$BIN" "$PATH_JSON" > "$RUN_DIR/native_root_output.txt" <<'PY'
import json, subprocess, sys
bin_path, path_json = sys.argv[1], sys.argv[2]
p = json.load(open(path_json, 'r', encoding='utf-8'))
cmd = [bin_path, 'root', p['leaf'], str(len(p['pathElements']))]
for e, i in zip(p['pathElements'], p['pathIndex']):
    cmd += [e, str(i)]
out = subprocess.check_output(cmd, text=True).strip()
print(out)
if out != p['root']:
    raise SystemExit(f"root mismatch: {out} != {p['root']}")
PY
ROOT_NATIVE_HEX="$(cat "$RUN_DIR/native_root_output.txt")"
ROOT_EXPECTED_HEX="$(jq -r '.root' "$PATH_JSON")"
[[ "$ROOT_NATIVE_HEX" == "$ROOT_EXPECTED_HEX" ]] || { echo "root mismatch: $ROOT_NATIVE_HEX != $ROOT_EXPECTED_HEX" >&2; exit 1; }
echo "[step1] path/root aligned"

jq -n \
  --arg cidHex "$CID_NATIVE_HEX" \
  --arg leafHex "$LEAF_NATIVE_HEX" \
  --arg rootHex "$ROOT_NATIVE_HEX" \
  --arg cidWitness "$CID_WITNESS_DEC" \
  --arg leafWitness "$LEAF_WITNESS_DEC" \
  '{
    ok: true,
    native: {cidHex:$cidHex, leafHex:$leafHex, rootHex:$rootHex},
    stateLeafCheck: {cidDec:$cidWitness, leafDec:$leafWitness},
    checks: [
      "native cid/leaf matches known vector",
      "StateLeafCheck witness matches native cid/leaf",
      "native root matches provided path root"
    ]
  }' > "$REPORT_JSON"

echo "[step1] PASS"
echo "[step1] artifacts:"
echo "  run dir : $RUN_DIR"
echo "  report  : $REPORT_JSON"
echo "  witness : $WTNS_JSON"
