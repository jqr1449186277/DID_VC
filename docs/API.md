# API Overview

DID E2E exposes local development APIs for the bulletin board, committee nodes, verifier service, and trace helper. These APIs are intended for experiments and automated smoke tests, not for public production deployment.

The C++ CLI is the primary client. The HTTP APIs are documented so developers can inspect the design, debug flows, or add experiments.

## Response Formats

The bulletin-board service currently uses two response styles:

- JSON for read endpoints, TTSS metadata endpoints, trace publishing, and most debugging APIs.
- Semicolon-separated key/value text for the original register/update/recovery endpoints used by the C++ client.

Example text response:

```text
ok=1;accepted=1;opId=op_abc;status=ACCEPTED;ready=0;txHash=0x...
```

The API is pre-1.0. Field names are stable enough for included scripts, but they are not a third-party compatibility contract yet.

## Bulletin Board Service

Default base URL: `http://127.0.0.1:3000`

The bulletin-board service is responsible for:

- submitting identity transactions to the local chain
- maintaining the local Poseidon-Merkle mirror
- exposing active root, leaf, path, record, and readiness views
- anchoring TTSS metadata and trace results
- tracking asynchronous operation readiness

### `GET /health`

Returns service readiness and basic runtime information.

Typical use:

```bash
curl -fsS http://127.0.0.1:3000/health
```

### `GET /root`

Returns the active Merkle root and epoch known to the service.

Important fields:

- `root`: active root as `0x` 32-byte hex.
- `epoch`: root epoch.
- `depth`: configured Merkle depth.

### `GET /record`

Lookup an identity record by demo ID or hash.

```bash
curl 'http://127.0.0.1:3000/record?id=demo'
curl 'http://127.0.0.1:3000/record?idHash=0x...'
```

The returned record mirrors the contract state: `cid`, `owner`, `recovery`, `version`, `active`, `pkNormHash`, and `pkRecHash`.

### `GET /leaf`

Returns active leaf material for an ID. The leaf is derived from:

```text
leaf = Poseidon(cid, pkNormHash, pkRecHash, ver, active)
```

The C++ client uses this endpoint to compare local leaf computation with the service mirror.

### `GET /path`

Returns the Merkle inclusion path for an ID against the current active root.

Typical fields:

- `root`
- `epoch`
- `leaf`
- `pathElements`
- `pathIndex`

### `GET /readySnapshot`

Returns record, leaf, path, and root readiness in one response.

Useful query parameters:

- `id`: demo ID.
- `idHash`: 32-byte identity hash.
- `minVersion`: wait target for update or recovery flows.

The C++ client uses this endpoint to avoid racing transaction settlement, mirror rebuild, path generation, and root observation.

### `GET /registerStatus`

Returns operation status by `opId`. Register and recovery endpoints return `opId` values when they accept asynchronous work.

Important status-like fields include:

- `status`
- `accepted`
- `ready`
- `currentVersion`
- `currentRoot`
- `currentEpoch`
- `lastError`

### `POST /registerZk`

Registers a new DID-like identity state.

Representative JSON body:

```json
{
  "id": "demo",
  "cidHex": "0x...",
  "pkNormHash": "0x...",
  "pkRecHash": "0x...",
  "ownerSeedHex": "0x...",
  "recoverySeedHex": "0x...",
  "wait": 1,
  "confirmations": 1,
  "includeSnapshot": 1
}
```

Implementation notes:

- The service derives demo owner and recovery wallets from seed hex values.
- The contract stores controller addresses, not the seeds.
- The service stores seeds in local demo state so later flows can run without an external wallet.
- `ttssSetupMode` may be `deferred`, `inline`, or `off` when TTSS metadata attachment is requested by a setup flow.

### `POST /applyUpdateZk`

Applies a normal-controller update.

Representative JSON body:

```json
{
  "id": "demo",
  "newCidHex": "0x...",
  "pkNormHash": "0x...",
  "pkRecHash": "0x...",
  "ownerSeedHex": "0x...",
  "wait": 1
}
```

The contract requires `msg.sender` to match the current `owner` and requires `newVersion == oldVersion + 1`.

### `POST /applyRecoveryRotateZk`

Applies recovery rotation through the recovery controller.

Representative JSON body:

```json
{
  "id": "demo",
  "newCidHex": "0x...",
  "pkNormHash": "0x...",
  "pkRecHash": "0x...",
  "recoverySeedHex": "0x...",
  "newOwnerSeedHex": "0x...",
  "newRecoverySeedHex": "0x...",
  "oldVersion": 0,
  "newVersion": 1,
  "wait": 1,
  "includeSnapshot": 1
}
```

This project deliberately rotates both normal and recovery controllers during recovery. That differs from weaker sketches where recovery only updates one key; rotating both is necessary for reclaiming control after controller leakage.

### `POST /registerTTSSMeta`

Anchors TTSS metadata for the current identity version and root epoch.

Representative JSON body:

```json
{
  "id": "demo",
  "vkSetHash": "0x...",
  "metaHash": "0x...",
  "ver": 0,
  "epoch": 2,
  "wait": 1
}
```

The contract checks that `ver` equals the current record version and `epoch` equals the current root epoch.

### `POST /applyRecoveryRotateTTSS`

Anchors TTSS metadata after a recovery rotation. The endpoint is separate so TTSS rotation and identity recovery can be composed by the C++ flow.

### `GET /ttssMeta`

Returns effective TTSS metadata by `id` or `idHash`. The response includes both local and on-chain views when available.

```bash
curl 'http://127.0.0.1:3000/ttssMeta?id=demo'
```

### `POST /publishTrace`

Anchors a trace result digest.

Representative JSON body:

```json
{
  "id": "demo",
  "ver": 0,
  "epoch": 2,
  "accusedSetHash": "0x...",
  "proofHash": "0x...",
  "traceDigest": "0x...",
  "wait": 1
}
```

The contract checks that `ver` and `epoch` match the active record and root epoch.

### `POST /recomputeRoot`

Forces a local mirror refresh and root recomputation. This is a development/debug endpoint used when testing service state.

## Committee Node

Default base URLs: `http://127.0.0.1:8001` through `http://127.0.0.1:8006`

Committee nodes store signed TTSS share envelopes. Requests use the configured demo token.

### `GET /health`

Returns node health.

### `POST /setShareEnvelope`

Stores one signed share envelope.

The request body includes:

- `token`
- `shareEnvelope`

The node validates envelope shape and demo signature before storing it by ID hash, version, epoch, and guardian index.

### `GET /shareMeta`

Returns metadata for a stored share.

Query fields:

- `idHash`
- `ver`
- `epoch`
- `guardianIndex`

### `POST /shareForRecover`

Returns one active share envelope for recovery. The request body identifies the share and includes the demo token.

### `POST /shareForTrace`

Returns one active share envelope for trace challenge material. It uses the same envelope store as recovery but is routed separately so experiments can distinguish intent.

### `POST /invalidateShares`

Marks old shares inactive after a successful recovery rotation.

### `GET /debugListShares`

Development-only inspection endpoint for local debugging.

## Verifier Service

Default base URL: `http://127.0.0.1:3400`

The verifier service wraps Groth16 proof verification for the local ZK authentication circuit. It is used by C++ auth and recovery flows and is not a general DID resolver.

Main endpoint:

- `POST /verify`: verifies a proof bundle and public signal binding.
- `GET /health`: service health.

The verifier checks the public root, context hash, session public-key hash, epoch, public-signal normalization, and session transcript binding before returning acceptance to the client.

## Trace Helper

Default base URL: `http://127.0.0.1:4000`

The trace helper models leaked-share collection and trace verification for experiments. It should not be exposed as a trusted production service. The C++ trace flow and `hardhat/tracer_client.js` interact with it during `run_K*` experiments.

## Client Entry Points

The C++ client exposes these modes:

- `--zk_auth_e2e`
- `--zk_recovery_e2e`
- `--ttss_setup`
- `--ttss_recover`
- `--ttss_recover_and_rotate`
- `--ttss_trace`
- `--ttss_trace_publish`

Use `./build/did_demo_zk --help` for the complete CLI option list.
