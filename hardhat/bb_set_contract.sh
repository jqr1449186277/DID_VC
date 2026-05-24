#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "Usage: $0 <CONTRACT_ADDR> [RPC_URL] [WS_URL]" >&2
  exit 1
fi

CONTRACT="$1"
RPC_URL="${2:-http://127.0.0.1:8545}"
WS_URL="${3:-}"

data="{\"contract\":\"${CONTRACT}\",\"rpcUrl\":\"${RPC_URL}\""
if [[ -n "$WS_URL" ]]; then
  data="${data},\"wsUrl\":\"${WS_URL}\""
fi
data="${data}}"

curl -sS -X POST "http://127.0.0.1:3000/config" \
  -H "Content-Type: application/json" \
  -d "${data}"
echo
