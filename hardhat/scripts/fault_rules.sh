#!/usr/bin/env bash
set -euo pipefail

# 用法：
#   sudo ./fault_rules.sh drop 8006
#   sudo ./fault_rules.sh drop 8005 8006
#   sudo ./fault_rules.sh reject 8005 8006
#   sudo ./fault_rules.sh clear

MODE="${1:-}"
shift || true

apply_drop_one() {
  local p="$1"
  iptables -A OUTPUT -o lo -p tcp --dport "$p" -j DROP
  iptables -A INPUT  -i lo -p tcp --dport "$p" -j DROP
}

apply_reject_one() {
  local p="$1"
  iptables -A OUTPUT -o lo -p tcp --dport "$p" -j REJECT --reject-with tcp-reset
  iptables -A INPUT  -i lo -p tcp --dport "$p" -j REJECT --reject-with tcp-reset
}

clear_ports() {
  for p in 8001 8002 8003 8004 8005 8006; do
    iptables -D OUTPUT -o lo -p tcp --dport "$p" -j DROP 2>/dev/null || true
    iptables -D INPUT  -i lo -p tcp --dport "$p" -j DROP 2>/dev/null || true
    iptables -D OUTPUT -o lo -p tcp --dport "$p" -j REJECT --reject-with tcp-reset 2>/dev/null || true
    iptables -D INPUT  -i lo -p tcp --dport "$p" -j REJECT --reject-with tcp-reset 2>/dev/null || true
  done
}

case "$MODE" in
  drop)
    clear_ports
    for p in "$@"; do apply_drop_one "$p"; done
    iptables -S | egrep '800[1-6].*(DROP|REJECT)' || true
    ;;
  reject)
    clear_ports
    for p in "$@"; do apply_reject_one "$p"; done
    iptables -S | egrep '800[1-6].*(DROP|REJECT)' || true
    ;;
  clear)
    clear_ports
    echo "OK: cleared 8001-8006 DROP/REJECT rules"
    ;;
  *)
    echo "Usage: sudo $0 {drop|reject|clear} [ports...]"
    exit 1
    ;;
esac
