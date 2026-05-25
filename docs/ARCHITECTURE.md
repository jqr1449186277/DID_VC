# Architecture

DID E2E is a local end-to-end prototype for DID-like identity state, ZK authentication, threshold recovery, and trace-flow experiments. It is built as a research harness, not as a production DID method implementation.

## System Boundary

The project deliberately separates three layers:

- On-chain anchor: `DIDBulletinBoardZK.sol` stores identity records, the active Merkle root, TTSS metadata hashes, and trace anchors.
- Off-chain services: Node.js services maintain the local Merkle mirror, expose HTTP APIs, wrap proof verification, and simulate trace evidence.
- C++ client and committee services: the C++ CLI drives flows, while committee nodes store and serve TTSS share envelopes.

The chain is the source of truth for identity records and anchors. The off-chain mirror is a performance and experiment layer that turns chain records into Poseidon leaves, inclusion paths, and readiness snapshots.

## Component Map

```text
did_demo_zk CLI
  |-- HTTP --> bb_service_zk.js ---- JSON-RPC ---- DIDBulletinBoardZK.sol
  |-- HTTP --> zk_verify_service.js
  |-- HTTP --> committee_node :8001..8006
  |-- HTTP --> pirate_box.js

bb_service_zk.js
  |-- state_store.js       persistent local mirror state
  |-- merkle_tree.js       Poseidon-Merkle mirror and paths
  |-- chain_client.js      ethers contract wrapper
  |-- routes_register.js   register/update/recovery endpoints
  |-- routes_ttss.js       TTSS metadata endpoints
  |-- routes_trace.js      trace anchor endpoint
```

## Main Components

- `did_demo_zk`: C++ CLI for ZK auth, ZK recovery, TTSS setup/recover/rotate, trace, and trace publish flows.
- `committee_node`: C++ HTTP service that stores signed TTSS share envelopes and serves recovery or trace requests.
- `bb_service_zk.js`: bulletin-board API and local Merkle mirror backed by the local Hardhat chain.
- `zk_verify_service.js`: local Groth16 verifier wrapper used by authentication and recovery flows.
- `pirate_box.js`: trace-flow helper that stores leaked-share material for experiments.
- Hardhat chain: local Ethereum-compatible chain for contract deployment and transactions.

## On-Chain State

The contract stores each identity under `bytes32 idHash`:

- `cid`: commitment or DID pointer used in the ZK leaf.
- `owner`: normal update controller.
- `recovery`: recovery controller.
- `version`: monotonic identity version.
- `active`: whether the record participates in the active root.
- `pkNormHash`: field/hash form of the normal key.
- `pkRecHash`: field/hash form of the recovery key.

Additional anchors:

- `activeRoot`, `rootEpoch`: current Poseidon-Merkle root and epoch.
- TTSS metadata: `vkSetHash`, `metaHash`, version, and epoch.
- Trace anchor: accused-set hash, proof hash, trace digest, version, and epoch.

## Off-Chain Mirror

`bb_service_zk.js` maintains local state in the Hardhat directory:

- `mirror`: idHash to active identity record.
- `idByHash`: reversible local mapping for demo IDs.
- `ops` and `requestToOp`: asynchronous operation status and idempotency.
- `ttssMeta`: local view of TTSS metadata anchors.
- `traceAnchors`: local view of trace anchors.
- `rootCache`, `pathCache`, `leafCache`: Merkle readiness caches.

The mirror is rebuilt from chain events and direct record reads. Readiness endpoints let the C++ client wait until a transaction, leaf, path, and root view are consistent.

## C++ Layout

- `main_cli.*`: CLI parsing and mode selection.
- `did_app_types.hpp`: shared DTOs used by client flows.
- `app_crypto.*`, `url_utils.*`, `csv_utils.*`, `app_paths.*`, `identity_state.*`: app-level helpers.
- `text_utils.*`, `json_utils.*`, `hex_utils.*`, `normalize_utils.*`, `process_utils.*`: shared utility layer.
- `http_transport.*`, `http_client.*`: HTTP transport, bulletin-board client, and readiness polling.
- `committee_client.*`: committee-node client operations.
- `zk_auth_flow.*`: ZK auth and ZK recovery orchestration.
- `ttss_flow.*`, `ttss_setup_flow.*`, `ttss_rotate_flow.*`: TTSS setup, recovery, and rotation.
- `ttss_artifacts.*`, `ttss_meta_registrar.*`: TTSS output and metadata anchoring helpers.
- `trace_flow.*`, `ttss_trace.*`: trace, trace verification, and trace publish support.
- `zk_backend.*`, `zk_paths.*`, `zk_runner.*`, `zk_public_signals.*`, `verifier_wrap.*`: proof/witness runner and verifier integration.
- `merkle_poseidon.*`: Poseidon and Merkle helpers shared by tests and flows.

## Flow Summary

### Registration

1. The C++ client derives local demo identity material: `sid`, `rho`, normal key seed, recovery key seed, `cid`, `pkNormHash`, and `pkRecHash`.
2. `POST /registerZk` submits `idHash`, `cid`, key hashes, and demo controller seeds to the bulletin board.
3. The bulletin-board service submits `registerZK` to the contract, mirrors the record, rebuilds the active root, and updates `rootEpoch`.
4. The client waits for `/readySnapshot`, `/leaf`, `/path`, and `/root` to agree.

### ZK Authentication

1. The client fetches the current root, leaf, and Merkle path.
2. It prepares witness input for `auth_membership.circom`.
3. It runs the Groth16 proof backend and receives `proof.json` plus public signals.
4. It sends the proof bundle to `zk_verify_service.js`.
5. The verifier checks public signal binding, root/epoch/context/session fields, and Groth16 proof validity.

### ZK Recovery

1. The client proves the old identity state is no longer accepted after recovery.
2. It uses the recovery controller to call `applyRecoveryRotateZk`.
3. The recovery path rotates both the normal controller and recovery controller.
4. The service rebuilds the root; the client waits for the new path and verifies the new proof succeeds.

### TTSS Setup And Recovery

1. `--ttss_setup` registers identity state and generates TTSS share envelopes for `TTSS_N` guardians with threshold `TTSS_T`.
2. The client distributes signed share envelopes to committee nodes with `/setShareEnvelope`.
3. TTSS metadata is anchored through `/registerTTSSMeta`.
4. `--ttss_recover` fetches active shares from committee nodes and reconstructs the recovery seed once at least `TTSS_T` distinct active shares are available.

### TTSS Recover And Rotate

1. The client recovers the old recovery seed from active committee shares.
2. It submits a recovery rotate to update identity state and both controller keys.
3. It invalidates old committee shares.
4. It generates and distributes new active shares.
5. It anchors new TTSS metadata for the new identity version and root epoch.

### Trace Flow

1. Trace experiments load or simulate leaked share material in `pirate_box.js`.
2. `--ttss_trace` gathers honest challenge shares and leaked material.
3. The trace algorithm builds an accused set and verification result.
4. `--ttss_trace_publish` anchors trace hashes through `/publishTrace`.

## Runtime Lifecycle

1. `scripts/dev.sh up` loads `config/dev.env`.
2. `scripts/start_stop.sh` starts a local Hardhat node.
3. The deploy script deploys `DIDBulletinBoardZK`.
4. The verifier, bulletin-board service, pirate box, and committee nodes are started.
5. `scripts/dev.sh smoke` runs a TTSS setup and checks leaf plus TTSS metadata readiness.
6. Runtime URLs and defaults are written to `run/ttss_phase5_env.sh`.

## Experiments

Research and batch scripts live under `experiments/` so the stable `scripts/` directory stays focused on build and development operations. `experiments/runs/` contains maintained scenario drivers, `experiments/lib/` contains instrumentation helpers, and `experiments/tools/` contains result summarizers.

See `docs/EXPERIMENTS.md` for the current maintained runners.

## Configuration

The native stack reads `config/dev.env` by default. Set `DID_E2E_CONFIG=/path/to/file.env` to use another configuration file. Environment variables remain override-friendly because `config/dev.env` uses shell default expansion. See `docs/CONFIGURATION.md` for the maintained configuration surface.
