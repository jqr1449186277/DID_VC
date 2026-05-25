# Configuration

DID E2E is configured through shell-style environment files and ordinary environment-variable overrides. The default native configuration is `config/dev.env`.

## Files

- `.env.example`: documented template for local development and Docker Compose.
- `config/dev.env`: default native development values used by `scripts/dev.sh`.
- `run/ttss_phase5_env.sh`: generated runtime file written by `scripts/dev.sh up`; source it when running examples manually.

`scripts/dev_config.sh` is the central loader. It requires the main path, port, TTSS, timeout, and binary variables to be present after loading the env file. Scripts should consume those variables instead of redefining scattered defaults.

## Selecting A Config

Use the default config:

```bash
scripts/dev.sh up
```

Use a custom config:

```bash
cp .env.example config/my-dev.env
DID_E2E_CONFIG=config/my-dev.env scripts/dev.sh up
```

Direct environment variables still override file defaults:

```bash
BB_PORT=3010 TTSS_N=4 TTSS_T=3 COMMITTEE_PORTS="8001 8002 8003 8004" scripts/dev.sh up
```

## Main Settings

Path settings:

- `PROJECT_ROOT`: repository root used by C++ and JS helpers.
- `CPP_DIR`: C++ source directory.
- `HARDHAT_DIR`: Hardhat contract and service directory.
- `SCRIPTS_DIR`: script directory.
- `BUILD_DIR`: native binary output directory.
- `RUN_DIR`: generated process state and env files.
- `LOG_DIR`: service log directory.
- `RESULTS_DIR`: generated experiment and smoke outputs.

Network settings:

- `RPC_URL`: local chain URL.
- `BB_PORT`: bulletin-board service port.
- `BASE_URL`: bulletin-board service URL.
- `DIDZK_VERIFY_SERVICE_HOST`: verifier service bind host.
- `DIDZK_VERIFY_SERVICE_PORT`: verifier service port.
- `DIDZK_VERIFY_SERVICE_URL`: verifier proof-check endpoint.
- `DIDZK_VERIFY_SERVICE_HEALTH`: verifier health endpoint.
- `VERIFY_HOST`, `VERIFY_PORT`, `VERIFY_URL`, `VERIFY_HEALTH`: compatibility aliases used by scripts and experiments.
- `PIRATE_PORT`: trace helper service port.
- `PIRATE_URL`: trace helper service URL.
- `COMMITTEE_PORTS`: space-separated committee-node ports to start.

Protocol settings:

- `TTSS_N`: number of TTSS guardians used by default flows.
- `TTSS_T`: TTSS threshold used by default flows.
- `TOKEN`: local demo token used by committee requests.
- `TREE_DEPTH`: Merkle-tree depth for the active compiled circuit assets.

Service entrypoint settings:

- `MAIN_BIN`: C++ CLI path.
- `COMMITTEE_BIN`: committee-node binary path.
- `TRACER_JS`: trace client path.
- `PIRATE_JS`: trace helper service path.
- `BB_SERVICE_JS`: bulletin-board service path.
- `VERIFIER_JS`: verifier service path.
- `DEPLOY_SCRIPT`: Hardhat deploy script path.

Runtime state settings:

- `ENV_FILE`: generated runtime env file.
- `SMOKE_STATE_FILE`: last smoke metadata file.
- `NODE_PID_FILE`, `SERVICE_PID_FILE`, `VERIFIER_PID_FILE`, `PIRATE_PID_FILE`: service PID files.
- `COMMITTEE_PID_PREFIX`: committee-node PID prefix.

## Timeouts

Most flow scripts accept these timeout knobs:

- `TIMEOUT_MS`
- `REGISTER_WAIT_MS`
- `PATH_WAIT_MS`
- `ROOT_WAIT_MS`
- `ROOT_POLL_MS`

Use larger waits on slower machines or when running through HTTP instrumentation proxies.

## TTSS And Committee Consistency

`TTSS_N` should not exceed the number of ports in `COMMITTEE_PORTS`. For example:

```bash
TTSS_N=4
TTSS_T=3
COMMITTEE_PORTS="8001 8002 8003 8004"
```

The development stack exports `COMMITTEE_URLS` in `run/ttss_phase5_env.sh` after startup. Experiment runners and manual CLI commands normally use that generated value.

## ZK Depth Consistency

`TREE_DEPTH` must match the compiled circuit assets under `zk_build/`. The default open-source workflow targets depth `20`. Different tree depths require regenerating matching circuit artifacts and are treated as a separate experiment setup.

## Generated Paths

- `build/`: native C++ binaries.
- `zk_build/`: generated circuit, witness, zkey, and verifier artifacts.
- `zk_inputs/`: generated proof inputs.
- `results/`: logs, smoke outputs, experiment outputs, and generated reports.
- `run/`: process IDs, runtime env files, and last smoke metadata.

These paths are generated local state and should not be committed.

## Native Versus Docker

Docker Compose uses `.env.example`-style defaults inside the container, while native development normally uses `config/dev.env`. Keep both files aligned when adding a new required setting.
