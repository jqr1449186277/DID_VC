#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="${PROJECT_ROOT:-$(cd "$SCRIPT_DIR/.." && pwd)}"
export PROJECT_ROOT

# shellcheck source=scripts/dev_config.sh
source "$SCRIPT_DIR/dev_config.sh"

exec "$SCRIPTS_DIR/start_stop.sh" "${1:-status}"
