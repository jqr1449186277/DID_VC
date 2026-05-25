# Configuration

DID E2E is configured through shell-style environment files and ordinary environment-variable overrides. The default native configuration is `config/dev.env`.

## Files

- `.env.example`: documented template for local development and Docker Compose.
- `config/dev.env`: default native development values used by `scripts/dev.sh`.
- `run/ttss_phase5_env.sh`: generated runtime file written by `scripts/dev.sh up`; source it when running examples manually.

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
- `TTSS_N`: number of TTSS guardians used by default flows.
- `TTSS_T`: TTSS threshold used by default flows.
- `TOKEN`: local demo token used by committee requests.
- `TREE_DEPTH`: Merkle-tree depth for the active compiled circuit assets.
- `PROJECT_ROOT`: repository root used by C++ and JS helpers.

## Timeouts

Most flow scripts accept these timeout knobs:

- `TIMEOUT_MS`
- `REGISTER_WAIT_MS`
- `PATH_WAIT_MS`
- `ROOT_WAIT_MS`
- `ROOT_POLL_MS`

Use larger waits on slower machines or when running through HTTP instrumentation proxies.

## Generated Paths

- `build/`: native C++ binaries.
- `zk_build/`: generated circuit, witness, zkey, and verifier artifacts.
- `zk_inputs/`: generated proof inputs.
- `results/`: logs, smoke outputs, experiment outputs, and generated reports.
- `run/`: process IDs, runtime env files, and last smoke metadata.

These paths are generated local state and should not be committed.
