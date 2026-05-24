#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

PROJECT_ROOT="${PROJECT_ROOT:-$(cd "$(dirname "$0")/.." && pwd)}"
BUILD_DIR="${BUILD_DIR:-$PROJECT_ROOT/zk_build}"
INPUT_DIR="${INPUT_DIR:-$PROJECT_ROOT/zk_inputs}"
RESULTS_DIR="${RESULTS_DIR:-$PROJECT_ROOT/results}"
PORT="${PORT:-3000}"
BASE_URL="${BASE_URL:-http://127.0.0.1:$PORT}"
TEST_ID="${TEST_ID:-alice_leafcheck}"
VER="${VER:-0}"
ACTIVE="${ACTIVE:-1}"
SID="${SID:-12345}"
RHO="${RHO:-67890}"
PK_NORM_HASH="${PK_NORM_HASH:-111111}"
PK_REC_HASH="${PK_REC_HASH:-222222}"
OWNER_SEED_HEX="${OWNER_SEED_HEX:-0x59c6995e998f97a5a0044966f094538f5d7f6b55b08f5cb7e6a3f8a6f8d1b9a9}"
RECOVERY_SEED_HEX="${RECOVERY_SEED_HEX:-0x5de4111afa1a4b94908d8a8ffcf147cb7e5ab7db04b2d30f082e9f65d9d6f5d6}"

RUN_TAG="$(date +%Y%m%d_%H%M%S)_state_leaf_check_${TEST_ID}"
RUN_DIR="$RESULTS_DIR/$RUN_TAG"
VECTOR_JSON="$RUN_DIR/vector.json"
INPUT_JSON="$RUN_DIR/state_leaf_check_input.json"
LEAF_JSON="$RUN_DIR/service_leaf.json"
WITNESS_BIN="$RUN_DIR/witness.wtns"
WITNESS_JSON="$RUN_DIR/witness.json"
RUN_LOG="$RUN_DIR/run.log"

mkdir -p "$RUN_DIR" "$INPUT_DIR"
exec > >(tee -a "$RUN_LOG") 2>&1

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

hex_to_dec() {
  node -e 'console.log(BigInt(process.argv[1]).toString(10))' "$1"
}

echo "[leaf-check] PROJECT_ROOT=$PROJECT_ROOT"
echo "[leaf-check] RUN_DIR=$RUN_DIR"

need_cmd curl
need_cmd jq
need_cmd node
need_cmd snarkjs
need_cmd awk
need_cmd grep

STATE_CPP="$BUILD_DIR/state_leaf_check/state_leaf_check_cpp/state_leaf_check"
STATE_R1CS="$BUILD_DIR/state_leaf_check/state_leaf_check.r1cs"
STATE_SYM="$BUILD_DIR/state_leaf_check/state_leaf_check.sym"

need_file "$STATE_CPP"
need_file "$STATE_R1CS"
need_file "$STATE_SYM"

HEALTH_JSON="$(curl -fsS "$BASE_URL/health")"
echo "[leaf-check] health: $HEALTH_JSON"

node "$PROJECT_ROOT/scripts/gen_vectors.mjs" \
  --sid "$SID" \
  --rho "$RHO" \
  --pkNormHash "$PK_NORM_HASH" \
  --pkRecHash "$PK_REC_HASH" \
  --ver "$VER" \
  --active "$ACTIVE" \
  --out "$VECTOR_JSON"

echo "[leaf-check] vector saved: $VECTOR_JSON"

CID_HEX_LOCAL="$(jq -r '.cidHex' "$VECTOR_JSON")"
CID_DEC_LOCAL="$(jq -r '.cidDec' "$VECTOR_JSON")"
LEAF_HEX_LOCAL="$(jq -r '.leafHex' "$VECTOR_JSON")"
LEAF_DEC_LOCAL="$(jq -r '.leafDec' "$VECTOR_JSON")"
PK_NORM_HEX="$(jq -r '.pkNormHashHex' "$VECTOR_JSON")"
PK_REC_HEX="$(jq -r '.pkRecHashHex' "$VECTOR_JSON")"

set +e
REG_OUT="$(curl -fsS -X POST "$BASE_URL/registerZk" \
  -H 'Content-Type: application/json' \
  -d "{\"id\":\"$TEST_ID\",\"cidHex\":\"$CID_HEX_LOCAL\",\"pkNormHash\":\"$PK_NORM_HEX\",\"pkRecHash\":\"$PK_REC_HEX\",\"ownerSeedHex\":\"$OWNER_SEED_HEX\",\"recoverySeedHex\":\"$RECOVERY_SEED_HEX\",\"wait\":1}" 2>/dev/null)"
REG_RC=$?
set -e
if [ "$REG_RC" -eq 0 ]; then
  echo "[leaf-check] registerZk: $REG_OUT"
else
  echo "[leaf-check] registerZk skipped or failed; continue with existing record"
fi

curl -fsS "$BASE_URL/leaf?id=$TEST_ID" > "$LEAF_JSON"
echo "[leaf-check] service leaf saved: $LEAF_JSON"

CID_HEX_SERVICE="$(jq -r '.cid' "$LEAF_JSON")"
LEAF_HEX_SERVICE="$(jq -r '.leaf' "$LEAF_JSON")"
CID_DEC_SERVICE="$(hex_to_dec "$CID_HEX_SERVICE")"
LEAF_DEC_SERVICE="$(hex_to_dec "$LEAF_HEX_SERVICE")"

if [ "$CID_HEX_LOCAL" != "$CID_HEX_SERVICE" ]; then
  echo "[leaf-check] cid mismatch" >&2
  echo "  local  : $CID_HEX_LOCAL" >&2
  echo "  service: $CID_HEX_SERVICE" >&2
  exit 1
fi
if [ "$LEAF_HEX_LOCAL" != "$LEAF_HEX_SERVICE" ]; then
  echo "[leaf-check] leaf mismatch" >&2
  echo "  local  : $LEAF_HEX_LOCAL" >&2
  echo "  service: $LEAF_HEX_SERVICE" >&2
  exit 1
fi

echo "[leaf-check] cid match ok:  $CID_HEX_LOCAL"
echo "[leaf-check] leaf match ok: $LEAF_HEX_LOCAL"

jq -n \
  --arg sid "$(jq -r '.sid' "$VECTOR_JSON")" \
  --arg rho "$(jq -r '.rho' "$VECTOR_JSON")" \
  --arg pkNormHash "$(jq -r '.pkNormHash' "$VECTOR_JSON")" \
  --arg pkRecHash "$(jq -r '.pkRecHash' "$VECTOR_JSON")" \
  --arg ver "$(jq -r '.ver' "$VECTOR_JSON")" \
  --arg active "$(jq -r '.active' "$VECTOR_JSON")" \
  '{sid:$sid, rho:$rho, pkNormHash:$pkNormHash, pkRecHash:$pkRecHash, ver:$ver, active:$active}' > "$INPUT_JSON"

echo "[leaf-check] input json saved: $INPUT_JSON"
cat "$INPUT_JSON"

"$STATE_CPP" "$INPUT_JSON" "$WITNESS_BIN"
echo "[leaf-check] witness saved: $WITNESS_BIN"

snarkjs wtns check "$STATE_R1CS" "$WITNESS_BIN"

snarkjs wtns export json "$WITNESS_BIN" "$WITNESS_JSON" >/dev/null
CID_WIRE_IDX="$(awk -F, '$4=="main.cid" {print $1; exit}' "$STATE_SYM")"
LEAF_WIRE_IDX="$(awk -F, '$4=="main.leaf" {print $1; exit}' "$STATE_SYM")"

if [ -z "$CID_WIRE_IDX" ] || [ -z "$LEAF_WIRE_IDX" ]; then
  echo "[leaf-check] failed to resolve main.cid/main.leaf in $STATE_SYM" >&2
  exit 1
fi

CID_DEC_WITNESS="$(jq -r --argjson idx "$CID_WIRE_IDX" '.[$idx]' "$WITNESS_JSON")"
LEAF_DEC_WITNESS="$(jq -r --argjson idx "$LEAF_WIRE_IDX" '.[$idx]' "$WITNESS_JSON")"

if [ "$CID_DEC_LOCAL" != "$CID_DEC_WITNESS" ]; then
  echo "[leaf-check] cid witness mismatch" >&2
  echo "  local  : $CID_DEC_LOCAL" >&2
  echo "  witness: $CID_DEC_WITNESS" >&2
  exit 1
fi
if [ "$LEAF_DEC_LOCAL" != "$LEAF_DEC_WITNESS" ]; then
  echo "[leaf-check] leaf witness mismatch" >&2
  echo "  local  : $LEAF_DEC_LOCAL" >&2
  echo "  witness: $LEAF_DEC_WITNESS" >&2
  exit 1
fi
if [ "$CID_DEC_LOCAL" != "$CID_DEC_SERVICE" ]; then
  echo "[leaf-check] cid service decimal mismatch" >&2
  echo "  local  : $CID_DEC_LOCAL" >&2
  echo "  service: $CID_DEC_SERVICE" >&2
  exit 1
fi
if [ "$LEAF_DEC_LOCAL" != "$LEAF_DEC_SERVICE" ]; then
  echo "[leaf-check] leaf service decimal mismatch" >&2
  echo "  local  : $LEAF_DEC_LOCAL" >&2
  echo "  service: $LEAF_DEC_SERVICE" >&2
  exit 1
fi

echo "[leaf-check] witness cid match ok:  $CID_DEC_WITNESS"
echo "[leaf-check] witness leaf match ok: $LEAF_DEC_WITNESS"

echo "[leaf-check] PASS"
echo "[leaf-check] artifacts:"
echo "  vector : $VECTOR_JSON"
echo "  input  : $INPUT_JSON"
echo "  service: $LEAF_JSON"
echo "  wtns   : $WITNESS_BIN"
echo "  wtnsjs : $WITNESS_JSON"
echo "  log    : $RUN_LOG"