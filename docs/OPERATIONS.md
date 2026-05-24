# Operations

## Native Stack

```bash
scripts/build.sh all
scripts/dev.sh up
scripts/dev.sh status
scripts/dev.sh smoke
scripts/dev.sh down
```

scripts/dev.sh loads config/dev.env by default. Use another config file:

```bash
DID_E2E_CONFIG=config/my-dev.env scripts/dev.sh up
```

## Docker Stack

```bash
docker compose up --build
docker compose exec did-dev scripts/dev.sh status
docker compose exec did-dev scripts/dev.sh smoke
docker compose down
```

## Logs and State

- Runtime env: `run/ttss_phase5_env.sh`
- Last smoke result: `run/ttss_phase5_smoke_last.json`
- Logs: `results/_logs/`
- Generated flow outputs: `results/`

## Common Failures

- Missing `hardhat/node_modules`: run `cd hardhat && npm install`, or use Docker Compose.
- Port already in use: adjust ports in `config/dev.env` or stop the conflicting service.
- Stale native processes: run `scripts/dev.sh down`, then `scripts/dev.sh up`.
- ZK backend path issues: ensure `PROJECT_ROOT`, `zk_build/`, and generated proof assets match the current tree depth.

## Experiments

Experiment runners live under experiments/runs/. Start the native or Docker stack first, then invoke the desired runner from the repository root. See experiments/README.md for the directory map and examples.
