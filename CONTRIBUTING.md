# Contributing

Thanks for taking a look at DID E2E.

## Development Setup

Use Docker for the shortest path:

```bash
docker compose up --build
```

For native development, install the dependencies listed in `README.md`, then run:

```bash
scripts/build.sh all
scripts/dev.sh up
scripts/dev.sh smoke
```

## Checks Before a PR

```bash
bash -n scripts/*.sh
CXXFLAGS_EXTRA="-Wall -Wextra -Wpedantic" scripts/build.sh all
./build/did_demo_zk --help
scripts/dev.sh smoke
```

## Coding Guidelines

- Keep shared C++ helpers in `cpp/text_utils.*`, `cpp/json_utils.*`, and `cpp/process_utils.*`.
- Prefer argv-based process execution through `process_utils` over shell string construction.
- Keep scripts as orchestration wrappers; put stable behavior in C++ or small reusable helpers.
- Do not commit generated runtime outputs from `build/`, `run/`, `logs/`, `results/`, `zk_inputs/`, or `zk_proofs/`.

## Security Notes

This repository is a local E2E demo and research harness. Do not use demo tokens, demo keys, or local Hardhat accounts in production systems.
