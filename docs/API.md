# API Overview

DID E2E exposes local development APIs for the bulletin board, committee nodes, verifier service, and trace helper. The APIs are intended for experiments and automated smoke tests, not for public production deployment.

## Bulletin Board Service

Default base URL: `http://127.0.0.1:3000`

Read endpoints:

- `GET /health` - service health
- `GET /root` - active Merkle root and epoch
- `GET /record?id=<id>` or `GET /record?idHash=<hex>` - identity record view
- `GET /leaf?id=<id>` - active leaf material
- `GET /path?id=<id>` - Merkle inclusion path
- `GET /readySnapshot?id=<id>&minVersion=<n>` - record, leaf, path, and root readiness snapshot
- `GET /registerStatus?opId=<id>` - asynchronous operation status
- `GET /ttssMeta?id=<id>` or `GET /ttssMeta?idHash=<hex>` - TTSS metadata anchor

Write endpoints:

- `POST /registerZk` - register an identity commitment and normal/recovery key hashes
- `POST /applyUpdateZk` - apply a normal-key identity update
- `POST /applyRecoveryRotateZk` - rotate normal and recovery control through the recovery path
- `POST /registerTTSSMeta` - anchor TTSS metadata for the current identity version and root epoch
- `POST /applyRecoveryRotateTTSS` - recovery rotate with TTSS metadata attachment
- `POST /publishTrace` - anchor a trace result digest
- `POST /recomputeRoot` - force a local Merkle mirror refresh

## Committee Node

Default base URLs: `http://127.0.0.1:8001` through `http://127.0.0.1:8006`

The committee node stores TTSS share envelopes and serves recovery/trace requests. Requests use the configured demo token in local development.

Common operations:

- health check
- store share envelope
- recover a share envelope
- serve trace challenge material
- invalidate old share material after rotation

## Verifier Service

Default base URL: `http://127.0.0.1:3400`

The verifier service wraps Groth16 proof verification for the local ZK authentication circuit. It is used by the C++ client flow and is not a general DID resolver.

## Trace Helper

Default base URL: `http://127.0.0.1:4000`

The trace helper models leaked-share collection and trace verification for experiments. It should not be exposed as a trusted production service.

## Stability

The API is pre-1.0 and experiment-facing. JSON shapes are stable enough for the included C++ client and scripts, but they are not a compatibility contract for third-party integrations yet.
