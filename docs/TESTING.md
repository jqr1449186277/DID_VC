# Testing

This project has three practical test layers: static checks, native C++ tests, and local end-to-end smoke flows.

## Quick Local Checks

```bash
bash -n scripts/*.sh experiments/runs/*.sh experiments/lib/*.sh
scripts/check_repo_format.sh
scripts/check_markdown_links.py
scripts/check_js_syntax.sh
CXXFLAGS_EXTRA="-Wall -Wextra -Wpedantic" scripts/build.sh all
./build/did_demo_zk --help
./build/committee_node --help
scripts/run_cpp_tests.sh
```

## Hardhat Checks

```bash
cd hardhat
npm ci
npx hardhat compile
```

If the Solidity compiler download is unavailable, CI falls back to a `solcjs` syntax compile for the contract.

## Development Smoke

```bash
scripts/dev.sh up
scripts/dev.sh smoke
scripts/dev.sh status
scripts/dev.sh down
```

The smoke flow starts the local chain, deploys the contract, starts services, runs TTSS setup, and validates leaf plus TTSS metadata readiness.

## Core C++ Coverage

`scripts/run_cpp_tests.sh` runs focused C++ checks for shared utilities, Poseidon/Merkle behavior, TTSS share recovery, and trace fixtures. It is the maintained replacement for older standalone experiment fixture scripts.

## CI Coverage

`.github/workflows/ci.yml` currently checks:

- repository formatting rules
- shell syntax under `scripts/` and `experiments/`
- JavaScript syntax with `scripts/check_js_syntax.sh`
- Markdown local links
- warning-clean C++ build
- CLI smoke-lite help output
- core C++ tests
- C++ sanitizer build and focused sanitizer tests
- Hardhat compile or `solcjs` fallback
- Docker Compose config validation

CI does not run the full local chain E2E suite because those flows are slower and require several cooperating services.
