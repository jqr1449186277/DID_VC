# Threat Model

DID E2E is an experimental local prototype. This document describes the assumptions used by the demo and the boundaries that are not protected.

## Assets

- identity state records anchored by the local bulletin-board contract
- owner and recovery key material generated for demo flows
- TTSS recovery shares stored by committee nodes
- ZK witness inputs, proving keys, verification keys, proofs, and public signals
- trace-flow evidence, accused sets, and trace anchors
- local chain transaction history and generated experiment outputs

## Trusted Components

- the local development machine or Docker container
- the local Hardhat chain and its pre-funded demo accounts
- the bulletin-board service process
- the verifier service process
- committee nodes configured with the local demo token
- generated circuit artifacts under `zk_build/`

These components are trusted for experiments only. The project does not attempt to harden them against a hostile production network.

## Assumptions

- The Hardhat chain is local and controlled by the operator.
- Demo tokens and demo keys are not secret enough for production use.
- TTSS recovery requires at least `TTSS_T` valid active shares from the configured `TTSS_N` guardians.
- Committee nodes are reachable through the configured local URLs.
- The active circuit artifacts, verification key, and verifier service match the tree depth used by the client flow.
- The Groth16 setup is experimental unless the operator replaces it with a trusted ceremony appropriate for their deployment.

## In-Scope Threats For Experiments

- invalid or stale Merkle roots
- malformed identity records, leaves, paths, or public signals
- insufficient or inactive TTSS shares
- TTSS rotation that fails to invalidate old shares
- trace-flow metadata tampering
- local service integration failures that corrupt identity state

## Out Of Scope

- production key custody
- secure remote deployment
- malicious host operating system behavior
- public internet API exposure
- denial-of-service hardening
- side-channel resistance
- wallet integration
- W3C Verifiable Credential issuance or presentation security
- real-world anonymity guarantees

## Privacy And Anonymity Boundary

The ZK auth flow demonstrates membership proof against an active identity root. It does not provide a complete privacy system. Metadata, local logs, trace-flow artifacts, network timing, and operator-controlled services can still reveal information.

The trace flow is a research simulation. It models leaked-share tracing and anchoring, but it is not a complete abuse-reporting, dispute-resolution, or privacy-preserving accountability protocol.

## Production Warning

Do not use this repository to protect real identities, credentials, funds, regulated personal data, production keys, or production recovery shares. Treat all generated keys, tokens, and proofs as local experiment material.
