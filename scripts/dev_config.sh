#!/usr/bin/env bash

# Central config loader for the native development stack.
# Defaults belong in config/dev.env and .env.example; scripts should consume
# the loaded variables instead of redefining port/path/timeout defaults.

if [[ -z "${PROJECT_ROOT:-}" ]]; then
  if [[ -n "${SCRIPT_DIR:-}" ]]; then
    PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
  else
    PROJECT_ROOT="$(pwd)"
  fi
fi
export PROJECT_ROOT

CONFIG_FILE="${DID_E2E_CONFIG:-$PROJECT_ROOT/config/dev.env}"
export CONFIG_FILE

if [[ -f "$CONFIG_FILE" ]]; then
  set -a
  # shellcheck source=config/dev.env
  source "$CONFIG_FILE"
  set +a
else
  echo "[dev-config] FATAL: config file not found: $CONFIG_FILE" >&2
  echo "[dev-config] Create it from .env.example or set DID_E2E_CONFIG=/path/to/env" >&2
  exit 1
fi

required_dev_config_vars=(
  PROJECT_ROOT CPP_DIR HARDHAT_DIR SCRIPTS_DIR BUILD_DIR RUN_DIR LOG_DIR RESULTS_DIR
  RPC_URL BB_PORT BASE_URL PIRATE_PORT PIRATE_URL
  DIDZK_VERIFY_SERVICE_HOST DIDZK_VERIFY_SERVICE_PORT DIDZK_VERIFY_SERVICE_URL DIDZK_VERIFY_SERVICE_HEALTH
  VERIFY_HOST VERIFY_PORT VERIFY_URL VERIFY_HEALTH
  VERIFIER_JS TREE_DEPTH TOKEN COMMITTEE_PORTS TTSS_N TTSS_T
  WAIT_SEC TIMEOUT_MS REGISTER_WAIT_MS PATH_WAIT_MS ROOT_WAIT_MS ROOT_POLL_MS SMOKE_TTSS_SETUP
  MAIN_BIN COMMITTEE_BIN TRACER_JS PIRATE_JS BB_SERVICE_JS DEPLOY_SCRIPT HARDHAT_DEPLOY_COMPILE
  ENV_FILE SMOKE_STATE_FILE NODE_PID_FILE SERVICE_PID_FILE VERIFIER_PID_FILE PIRATE_PID_FILE COMMITTEE_PID_PREFIX DEV_LOG_PREFIX
)

for name in "${required_dev_config_vars[@]}"; do
  if [[ -z "${!name:-}" ]]; then
    echo "[dev-config] FATAL: missing required config variable: $name" >&2
    echo "[dev-config] config file: $CONFIG_FILE" >&2
    exit 1
  fi
done
