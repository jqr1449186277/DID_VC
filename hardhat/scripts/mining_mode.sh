#!/usr/bin/env bash
set -euo pipefail
RPC="${RPC:-http://127.0.0.1:8545}"

rpc() {
  local method="$1"; shift
  local params="$1"
  curl -s "$RPC" -H "Content-Type: application/json" \
    --data "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"$method\",\"params\":$params}"
  echo
}

case "${1:-}" in
  automine_on)
    rpc evm_setIntervalMining "[0]"
    rpc evm_setAutomine "[true]"
    ;;
  interval_ms)
    ms="${2:-2000}"
    rpc evm_setAutomine "[false]"
    rpc evm_setIntervalMining "[$ms]"
    ;;
  *)
    echo "Usage:"
    echo "  $0 automine_on"
    echo "  $0 interval_ms 2000"
    exit 1
    ;;
esac
