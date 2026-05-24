#!/usr/bin/env bash
set -euo pipefail

ROOT="${ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
CPP_DIR="${CPP_DIR:-$ROOT/cpp}"
TEST_DIR="${TEST_DIR:-$ROOT/build/tests}"
CXX="${CXX:-g++}"

mkdir -p "$TEST_DIR"

compile() {
  echo "[cpp-test] compile $1"
  "$CXX" -O2 -std=c++17 -I"$CPP_DIR" ${CXXFLAGS_EXTRA:-} "${@:2}"
}

ensure_poseidon_constants() {
  local constants="$ROOT/circuits/node_modules/circomlib/circuits/poseidon_constants.circom"
  if [[ -f "$constants" ]]; then
    return 0
  fi
  if ! command -v npm >/dev/null 2>&1; then
    echo "missing Poseidon constants and npm is not available" >&2
    return 1
  fi
  echo "[cpp-test] install circomlib for Poseidon constants"
  npm install --prefix "$ROOT/circuits" --no-save --no-package-lock circomlib >/tmp/did-e2e-circomlib-install.log
}

run_ttss_share_recovery_test() {
  local bin="$TEST_DIR/tests_ttss_share_rec"
  compile tests_ttss_share_rec \
    "$ROOT/tests/cpp/tests_ttss_share_rec.cpp" \
    "$CPP_DIR/ttss_nits_shamir.cpp" \
    "$CPP_DIR/share_envelope.cpp" \
    "$CPP_DIR/hex_utils.cpp" \
    "$CPP_DIR/text_utils.cpp" \
    "$CPP_DIR/json_utils.cpp" \
    -lgmpxx -lgmp \
    -o "$bin"
  "$bin"
}

run_ttss_trace_test() {
  local bin="$TEST_DIR/tests_ttss_trace"
  compile tests_ttss_trace \
    "$ROOT/tests/cpp/tests_ttss_trace.cpp" \
    "$CPP_DIR/ttss_trace.cpp" \
    "$CPP_DIR/ttss_nits_shamir.cpp" \
    "$CPP_DIR/share_envelope.cpp" \
    "$CPP_DIR/text_utils.cpp" \
    "$CPP_DIR/hex_utils.cpp" \
    "$CPP_DIR/json_utils.cpp" \
    -lgmpxx -lgmp \
    -o "$bin"
  "$bin"
}

run_ttss_fixture_test() {
  local bin="$TEST_DIR/gen_ttss_fixture"
  local out="$TEST_DIR/fixture"
  compile gen_ttss_fixture \
    "$ROOT/tests/cpp/gen_ttss_fixture.cpp" \
    "$CPP_DIR/ttss_nits_shamir.cpp" \
    "$CPP_DIR/share_envelope.cpp" \
    "$CPP_DIR/hex_utils.cpp" \
    "$CPP_DIR/text_utils.cpp" \
    "$CPP_DIR/json_utils.cpp" \
    -lgmpxx -lgmp \
    -o "$bin"
  rm -rf "$out"
  mkdir -p "$out"
  "$bin" "$out"
  for file in \
    share_envelope_0.json \
    set_share_envelope_req.json \
    req_recover.json \
    req_trace.json \
    req_invalidate.json; do
    python3 -m json.tool "$out/$file" >/dev/null
  done
  grep -q '^ID_HASH=0x1111111111111111111111111111111111111111111111111111111111111111$' "$out/fixture.env"
}

run_poseidon_test() {
  ensure_poseidon_constants
  local bin="$TEST_DIR/test_poseidon_native"
  compile test_poseidon_native \
    "$ROOT/tests/cpp/test_poseidon_native.cpp" \
    "$CPP_DIR/merkle_poseidon.cpp" \
    "$CPP_DIR/text_utils.cpp" \
    -lgmpxx -lgmp \
    -o "$bin"

  local sid="0x0000000000000000000000000000000000000000000000000000000000000001"
  local rho="0x0000000000000000000000000000000000000000000000000000000000000002"
  local pk_norm="0x0000000000000000000000000000000000000000000000000000000000000003"
  local pk_rec="0x0000000000000000000000000000000000000000000000000000000000000004"
  local out cid leaf root
  out="$(PROJECT_ROOT="$ROOT" "$bin" leaf "$sid" "$rho" "$pk_norm" "$pk_rec" 1 1)"
  cid="$(sed -n 's/^cid=//p' <<<"$out")"
  leaf="$(sed -n 's/^leaf=//p' <<<"$out")"
  [[ "$cid" =~ ^0x[0-9a-f]{64}$ ]] || { echo "bad cid: $cid" >&2; return 1; }
  [[ "$leaf" =~ ^0x[0-9a-f]{64}$ ]] || { echo "bad leaf: $leaf" >&2; return 1; }
  root="$(PROJECT_ROOT="$ROOT" "$bin" root "$leaf" 1 "$sid" 0)"
  [[ "$root" =~ ^0x[0-9a-f]{64}$ ]] || { echo "bad root: $root" >&2; return 1; }
}

run_ttss_share_recovery_test
run_ttss_trace_test
run_ttss_fixture_test
run_poseidon_test

echo "[cpp-test] PASS"
