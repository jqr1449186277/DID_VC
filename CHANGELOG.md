# Changelog

All notable changes to DID E2E are tracked here.

The project is a research prototype and has not published a stable release yet.

## Unreleased

- Reworked the repository into a cleaner open-source layout with standard docs, CI, Docker Compose, and native development entrypoints.
- Added Makefile-based native builds while keeping `scripts/build.sh` as the documented build wrapper.
- Split core C++ flow code and shared utilities, including JSON, text, hex, process, HTTP, TTSS, and ZK helpers.
- Added maintained experiment runners under `experiments/runs/` and removed legacy depth-sweep, fixture, and hard-coded figure scripts that no longer matched the current project layout.
- Documented project boundaries: experimental prototype, non-production use, current DID-like functionality, and missing W3C DID/VC features.
- Added CI coverage for shell syntax, warning-clean C++ builds, CLI smoke checks, core C++ tests, Hardhat compile, and Docker Compose validation.
- Added repository hygiene files, threat model, dependency notes, release checklist, Markdown link checks, JavaScript syntax checks, and C++ sanitizer CI for `v0.1.0` preparation.

## Initial Prototype

- Implemented a local DID-like identity state demo with Hardhat, a bulletin-board service, Poseidon-Merkle roots, Groth16 authentication, TTSS recovery/rotation, committee nodes, and trace-flow experiments.
