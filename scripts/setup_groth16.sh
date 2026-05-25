#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="${PROJECT_ROOT:-$(cd "$(dirname "$0")/.." && pwd)}"
BUILD_DIR="${BUILD_DIR:-$PROJECT_ROOT/zk_build}"
PTAU_DIR="$BUILD_DIR/ptau"
ZKEY_DIR="$BUILD_DIR/zkey"
VK_DIR="$BUILD_DIR/vk"
POT_POWER="${POT_POWER:-14}"
SNARKJS="${SNARKJS_BIN:-}"

if [ -z "$SNARKJS" ]; then
  if [ -x "$PROJECT_ROOT/hardhat/node_modules/.bin/snarkjs" ]; then
    SNARKJS="$PROJECT_ROOT/hardhat/node_modules/.bin/snarkjs"
  else
    SNARKJS="$(command -v snarkjs || true)"
  fi
fi

if [ -z "$SNARKJS" ]; then
  echo "[setup] snarkjs not found; run: cd hardhat && npm ci" >&2
  exit 127
fi

mkdir -p "$PTAU_DIR" "$ZKEY_DIR" "$VK_DIR"

if [ ! -f "$BUILD_DIR/auth_membership/auth_membership.r1cs" ]; then
  echo "[setup] missing auth_membership.r1cs; compiling circuit first"
  "$PROJECT_ROOT/scripts/compile_zk.sh"
fi

if [ ! -f "$PTAU_DIR/pot${POT_POWER}_final.ptau" ]; then
  echo "[setup] powers of tau"
  "$SNARKJS" powersoftau new bn128 "$POT_POWER" "$PTAU_DIR/pot${POT_POWER}_0000.ptau" -v
  "$SNARKJS" powersoftau contribute \
    "$PTAU_DIR/pot${POT_POWER}_0000.ptau" \
    "$PTAU_DIR/pot${POT_POWER}_0001.ptau" \
    --name="first contribution" -v <<<'zk-localchain'
  "$SNARKJS" powersoftau prepare phase2 \
    "$PTAU_DIR/pot${POT_POWER}_0001.ptau" \
    "$PTAU_DIR/pot${POT_POWER}_final.ptau" -v
fi

echo "[setup] groth16 setup"
"$SNARKJS" groth16 setup \
  "$BUILD_DIR/auth_membership/auth_membership.r1cs" \
  "$PTAU_DIR/pot${POT_POWER}_final.ptau" \
  "$ZKEY_DIR/auth_membership_0000.zkey"

"$SNARKJS" zkey contribute \
  "$ZKEY_DIR/auth_membership_0000.zkey" \
  "$ZKEY_DIR/auth_membership_final.zkey" \
  --name="final contribution" -v <<<'zk-localchain-final'

"$SNARKJS" zkey export verificationkey \
  "$ZKEY_DIR/auth_membership_final.zkey" \
  "$VK_DIR/auth_membership_vk.json"

echo "[setup] done"
