# DID E2E

End-to-end DID demo with ZK auth, TTSS recovery, trace flow, committee nodes, and a local Hardhat chain.

## Quick Start

```bash
docker compose up --build
```

The compose service installs Hardhat dependencies on first run, builds the C++ binaries, starts the local chain, deploys the DID bulletin board contract, starts the verifier, bulletin board service, pirate box, committee nodes, and runs the built-in smoke setup.

Check status:

```bash
docker compose exec did-dev scripts/dev.sh status
```

Run smoke again:

```bash
docker compose exec did-dev scripts/dev.sh smoke
```

Stop the stack:

```bash
docker compose down
```

## Native Development

The native stack reads `config/dev.env` by default. To start from the documented template:

```bash
cp .env.example config/my-dev.env
DID_E2E_CONFIG=config/my-dev.env scripts/dev.sh status
```

Install native dependencies:

```bash
sudo apt-get update
sudo apt-get install -y build-essential curl jq nodejs npm python3 libsodium-dev libgmp-dev nlohmann-json3-dev
cd hardhat
npm install
cd ..
```

Build:

```bash
scripts/build.sh all
```

Start:

```bash
scripts/dev.sh up
```

Smoke:

```bash
scripts/dev.sh smoke
```

Status:

```bash
scripts/dev.sh status
```

Stop:

```bash
scripts/dev.sh down
```

## Example Flows

After the stack is healthy:

```bash
source run/ttss_phase5_env.sh
./build/did_demo_zk --zk_auth_e2e --id demo_auth --runs 1 --workdir results/examples
./build/did_demo_zk --ttss_setup --id demo_ttss --bb "$BASE_URL" --committee_urls "$COMMITTEE_URLS" --committee_token "$TOKEN" --ttss_n "$TTSS_N" --ttss_t "$TTSS_T" --workdir results/examples
```

The smoke state is written to:

```bash
run/ttss_phase5_smoke_last.json
```

## Project Structure

- `cpp/`: C++ client, committee node, ZK/TTSS/trace flows, and shared utilities.
- `hardhat/`: smart contracts and Node.js services.
- `scripts/`: build, development stack, smoke, experiments, and result summarizers.
- `results/`: generated outputs and logs.
- `run/`: local process state and environment files.

## Development Notes

- Shared C++ helpers live in `cpp/text_utils.*`, `cpp/json_utils.*`, and `cpp/process_utils.*`.
- `scripts/dev_common.sh` contains shell helpers used by the development stack.
- `scripts/start_stop.sh` remains the native stack orchestrator; `scripts/dev.sh` is the public entrypoint.
- Architecture notes live in `docs/ARCHITECTURE.md`.
- Operational notes live in `docs/OPERATIONS.md`.
- CI is defined in `.github/workflows/ci.yml`.
