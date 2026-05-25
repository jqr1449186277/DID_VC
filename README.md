# DID E2E

End-to-end DID demo with ZK auth, TTSS recovery, trace flow, committee nodes, and a local Hardhat chain.

## Project Status

DID E2E is an experimental research prototype. It demonstrates identity-state registration, ZK anonymous authentication, TTSS recovery and rotation, trace anchoring, and a local end-to-end development stack.

Do not use this project in production. The repository uses local-chain accounts, demo tokens, development services, and experimental cryptographic flows. It has not been audited and does not protect real identities, credentials, funds, production keys, or regulated personal data.

## Scope

Currently supported:

- local DID-like identity state registration on a Hardhat chain
- normal-key update and recovery-key rotation paths
- Poseidon-Merkle root anchoring and path generation
- Groth16-backed ZK authentication against the active identity root
- TTSS share setup, recovery, rotation, and committee-node storage
- trace-flow simulation and trace result anchoring
- Docker/native development stack, smoke checks, and core C++ tests

Not yet supported:

- registered W3C DID method
- W3C-conformant DID Document resolver
- DID URL dereferencing
- DID deactivation operation
- W3C Verifiable Credential issuance, wallet storage, presentation, or credential status
- production key management, access control, monitoring, or deployment hardening

See `docs/DID_METHOD.md` for method-design notes, `docs/API.md` for local API details, `docs/CONFIGURATION.md` for runtime configuration, and `docs/THREAT_MODEL.md` for security assumptions.

## Design Overview

DID E2E is organized around a small local identity-state system:

- The Hardhat contract stores the canonical identity record keyed by `idHash` and anchors the active Poseidon-Merkle root plus root epoch.
- The bulletin-board service mirrors active records off-chain, builds Merkle leaves and paths, submits root updates, and exposes readiness APIs used by the C++ client.
- The C++ client drives registration, ZK authentication, recovery, TTSS setup/recovery/rotation, and trace publishing from a single CLI.
- Committee nodes store signed TTSS share envelopes and serve recovery or trace requests only when the share metadata is active.
- The trace helper models leaked-share evidence and lets experiments verify whether the trace flow identifies the expected guardian set.

The design is intentionally split between on-chain anchors and off-chain experiment services. The chain records identity state, TTSS metadata hashes, trace anchors, and active roots; generated keys, local Merkle caches, witness files, proofs, committee shares, and experiment logs stay in local services and generated output directories.

## Documentation Map

- `docs/ARCHITECTURE.md`: component map, state model, runtime lifecycle, and end-to-end flows.
- `docs/API.md`: local HTTP APIs, endpoint semantics, and example payloads.
- `docs/DID_METHOD.md`: how the current implementation maps to a possible future DID method and what W3C DID/VC pieces are missing.
- `docs/CONFIGURATION.md`: ports, paths, TTSS parameters, and timeout knobs.
- `docs/EXPERIMENTS.md`: maintained experiment scripts and result conventions.
- `docs/THREAT_MODEL.md`: trust assumptions, privacy boundary, and non-production warning.
- `docs/DEPENDENCIES.md`: native, npm, C++ library, and ZK tooling dependencies.
- `docs/TESTING.md`: local checks and CI coverage.
- `docs/RELEASE.md`: `v0.1.0` release checklist.

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
- `scripts/`: build, development-stack, ZK setup, and smoke-test entrypoints.
- `experiments/`: maintained research runners, instrumentation helpers, and result summarizers.
- `docs/`: API, architecture, configuration, testing, operations, and experiment notes.
- `results/`: generated outputs and logs.
- `run/`: local process state and environment files.

## Development Notes

- Shared C++ helpers live in `cpp/text_utils.*`, `cpp/json_utils.*`, and `cpp/process_utils.*`.
- `scripts/dev_common.sh` contains shell helpers used by the development stack.
- `scripts/start_stop.sh` remains the native stack orchestrator; `scripts/dev.sh` is the public entrypoint.
- Architecture notes live in `docs/ARCHITECTURE.md`.
- Operational notes live in `docs/OPERATIONS.md`.
- Configuration notes live in `docs/CONFIGURATION.md`.
- Test and CI notes live in `docs/TESTING.md`.
- Experiment runner notes live in `docs/EXPERIMENTS.md`.
- Threat model and dependency notes live in `docs/THREAT_MODEL.md` and `docs/DEPENDENCIES.md`.
- Release notes and checklist live in `CHANGELOG.md` and `docs/RELEASE.md`.
- CI is defined in `.github/workflows/ci.yml`.
