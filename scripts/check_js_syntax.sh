#!/usr/bin/env bash
set -euo pipefail

ROOT="${ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
cd "$ROOT"

count=0
while IFS= read -r -d '' file; do
  [[ "$file" != */node_modules/* ]] || continue
  node --check "$file"
  count=$((count + 1))
done < <(git ls-files -z '*.js' '*.mjs')

echo "[js-syntax] PASS ($count files)"
