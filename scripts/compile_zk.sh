#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="${PROJECT_ROOT:-$(cd "$(dirname "$0")/.." && pwd)}"
BUILD_DIR="${BUILD_DIR:-$PROJECT_ROOT/zk_build}"
CIRCUIT_NAME="${CIRCUIT_NAME:-auth_membership}"
CIRCUIT_FILE="${CIRCUIT_FILE:-$PROJECT_ROOT/circuits/${CIRCUIT_NAME}.circom}"
OUT_DIR="${OUT_DIR:-$BUILD_DIR/$CIRCUIT_NAME}"
NODE_MODULES_DIR="${NODE_MODULES_DIR:-$PROJECT_ROOT/hardhat/node_modules}"

if ! command -v circom >/dev/null 2>&1; then
  echo "[compile_zk] circom not found in PATH" >&2
  exit 127
fi

if [ ! -f "$CIRCUIT_FILE" ]; then
  echo "[compile_zk] circuit not found: $CIRCUIT_FILE" >&2
  exit 1
fi

if [ ! -f "$NODE_MODULES_DIR/circomlib/circuits/poseidon.circom" ]; then
  echo "[compile_zk] missing circomlib dependency under hardhat/node_modules" >&2
  echo "[compile_zk] run: cd hardhat && npm ci" >&2
  exit 1
fi

mkdir -p "$OUT_DIR"

echo "[compile_zk] compiling $CIRCUIT_FILE"
circom "$CIRCUIT_FILE" \
  --r1cs \
  --wasm \
  --sym \
  --output "$OUT_DIR" \
  -l "$NODE_MODULES_DIR"

echo "[compile_zk] wrote:"
echo "  $OUT_DIR/${CIRCUIT_NAME}.r1cs"
echo "  $OUT_DIR/${CIRCUIT_NAME}_js/${CIRCUIT_NAME}.wasm"
echo "  $OUT_DIR/${CIRCUIT_NAME}_js/generate_witness.js"
