#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [[ "$(basename "$SCRIPT_DIR")" == "scripts" ]]; then
  DEFAULT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
else
  DEFAULT_ROOT="$SCRIPT_DIR"
fi

ROOT="${ROOT:-${PROJECT_ROOT:-$DEFAULT_ROOT}}"
CPP_DIR="${CPP_DIR:-$ROOT/cpp}"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
TARGET="all"
MAKE_ARGS=()
if [[ $# -gt 0 ]]; then
  if [[ "$1" == -* ]]; then
    MAKE_ARGS=("$@")
  else
    TARGET="$1"
    shift
    MAKE_ARGS=("$@")
  fi
fi
MAKE_BIN="${MAKE_BIN:-make}"
CXX="${CXX:-g++}"
CXXFLAGS_EXTRA="${CXXFLAGS_EXTRA:-}"
LDFLAGS_EXTRA="${LDFLAGS_EXTRA:-}"

log(){ echo "[build_ttss] $*"; }
die(){ echo "[build_ttss] FATAL: $*" >&2; exit 1; }

[[ -d "$CPP_DIR" ]] || die "missing cpp dir: $CPP_DIR"
[[ -f "$ROOT/Makefile" ]] || die "missing Makefile: $ROOT/Makefile"

log "make target=$TARGET root=$ROOT"
exec "$MAKE_BIN" -C "$ROOT" \
  ROOT="$ROOT" \
  CPP_DIR="$CPP_DIR" \
  BUILD_DIR="$BUILD_DIR" \
  CXX="$CXX" \
  CXXFLAGS_EXTRA="$CXXFLAGS_EXTRA" \
  LDFLAGS_EXTRA="$LDFLAGS_EXTRA" \
  "${MAKE_ARGS[@]}" \
  "$TARGET"
