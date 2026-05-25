#!/usr/bin/env bash
set -euo pipefail

ROOT="${ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
cd "$ROOT"

tracked_source_files() {
  git ls-files --cached --others --exclude-standard -z -- \
    '*.md' '*.sh' '*.env' '*.example' '*.yml' '*.yaml' '*.json' \
    '*.js' '*.mjs' '*.py' '*.cpp' '*.hpp' '*.h' '*.c' '*.circom' \
    'Makefile' '.gitignore' '.dockerignore' '.gitattributes' '.editorconfig' '.clang-format'
}

fail=0

while IFS= read -r -d '' file; do
  [[ -f "$file" ]] || continue
  if LC_ALL=C grep -Iq . "$file"; then
    if grep -n $'\r' "$file" >/tmp/did-e2e-format-crlf.$$ 2>/dev/null; then
      echo "[format] CRLF detected: $file"
      fail=1
    fi
    if [[ -s "$file" && "$(tail -c 1 "$file")" != "" ]]; then
      echo "[format] missing final newline: $file"
      fail=1
    fi
    if [[ "$file" != *.md ]] && grep -nE '[[:blank:]]$' "$file" >/tmp/did-e2e-format-trailing.$$ 2>/dev/null; then
      echo "[format] trailing whitespace: $file"
      fail=1
    fi
  fi
done < <(tracked_source_files)

rm -f /tmp/did-e2e-format-crlf.$$ /tmp/did-e2e-format-trailing.$$

if command -v git >/dev/null 2>&1; then
  git diff --check
fi

if [[ "$fail" != "0" ]]; then
  echo "[format] FAILED"
  exit 1
fi

echo "[format] PASS"
