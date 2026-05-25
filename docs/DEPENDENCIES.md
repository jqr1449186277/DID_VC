# Dependencies

This document summarizes the main dependency groups needed to build and run DID E2E.

## Native System Packages

Typical Ubuntu packages:

```bash
sudo apt-get update
sudo apt-get install -y build-essential curl jq nodejs npm python3 libsodium-dev libgmp-dev nlohmann-json3-dev
```

CI also uses `clang` for the sanitizer job.

## C++ Libraries

- GMP / GMPXX: finite-field and big-integer operations.
- libsodium: demo hashing/signing helpers used by the local prototype.
- nlohmann/json: JSON parsing and serialization.
- cpp-httplib: vendored single-header HTTP helper under `cpp/httplib.h`.

## Node.js Packages

Node dependencies are managed under `hardhat/package.json` and locked by `hardhat/package-lock.json`.

Main groups:

- Hardhat and Ethereum helpers for the local chain and contract deployment.
- `snarkjs` for Groth16 proof tooling.
- `circomlibjs` and `circomlib` for Poseidon-related circuit/runtime support.
- service dependencies used by the bulletin-board, trace, and helper scripts.

Install them with:

```bash
cd hardhat
npm ci
```

## Circuit And ZK Tooling

- Circom source lives under `circuits/`.
- Generated circuit artifacts live under `zk_build/`.
- `scripts/compile_zk.sh` compiles supported circuits when needed.
- `scripts/setup_groth16.sh` prepares Groth16 artifacts for local development.

Generated ZK artifacts are local build products and should not be committed unless a release process explicitly calls for them.

## Docker

`Dockerfile` and `docker-compose.yml` provide the shortest reproducible development path. The container installs native packages, installs Hardhat dependencies, builds C++ binaries, starts the local services, and runs smoke checks.

## License Notes

The project source is MIT licensed. Third-party packages keep their own licenses. Before any production or public release that redistributes binaries or generated artifacts, review:

- `hardhat/package-lock.json`
- vendored headers under `cpp/`
- native package licenses from the target distribution
- circuit/snarkjs/circomlib artifact redistribution terms

The repository does not currently ship a generated SBOM.
