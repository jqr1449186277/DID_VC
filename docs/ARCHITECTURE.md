# Architecture

DID E2E is a local end-to-end demo for DID state registration, ZK authentication, TTSS recovery, and tracing.

## Components

- `did_demo_zk`: C++ CLI that drives ZK auth, recovery, TTSS setup/recover/rotate, and trace flows.
- `committee_node`: C++ HTTP service that stores TTSS share envelopes and serves recovery/trace endpoints.
- `bb_service_zk.js`: Node.js bulletin board API backed by a local Hardhat chain.
- `zk_verify_service.js`: local verifier service used by the C++ client.
- `pirate_box.js`: trace-flow helper service used to model leaked shares.
- Hardhat chain: local Ethereum-compatible chain for contract deployment and transaction flow.

## C++ Layout

- `main_cli.*`: CLI parsing and mode selection.
- `did_app_types.hpp`: shared DTOs used by the client flows.
- `text_utils.*`, `json_utils.*`, `process_utils.*`: shared utility layer.
- `http_client.cpp`: bulletin-board HTTP client and readiness polling.
- `committee_client.cpp`: committee-node client functions.
- `zk_auth_flow.cpp`: ZK auth and recovery flow orchestration.
- `ttss_flow.cpp`, `ttss_setup_flow.cpp`, `ttss_rotate_flow.cpp`: TTSS setup, recovery, and rotation.
- `trace_flow.cpp`: trace and trace publish orchestration.
- `zk_backend.*`, `verifier_wrap.*`, `merkle_poseidon.*`: proof/witness/field support.

## Experiments

Research and batch scripts live under `experiments/` so the stable `scripts/` directory stays focused on build and development operations. `experiments/runs/` contains maintained scenario drivers, `experiments/lib/` contains instrumentation helpers, and `experiments/tools/` contains result summarizers.

## Runtime Flow

1. `scripts/dev.sh up` starts a local Hardhat node.
2. `start_stop.sh` deploys the DID bulletin board contract.
3. The verifier, bulletin board, pirate box, and committee nodes are started.
4. `scripts/dev.sh smoke` runs a TTSS setup and checks the leaf plus TTSS metadata.
5. Example flows use `run/ttss_phase5_env.sh` to discover ports and URLs.

## Configuration

The native stack reads `config/dev.env` by default. Set `DID_E2E_CONFIG=/path/to/file.env` to use another configuration file. Environment variables always remain override-friendly because `config/dev.env` uses shell default expansion. See `docs/CONFIGURATION.md` for the maintained configuration surface.
