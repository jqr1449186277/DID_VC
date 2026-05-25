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
bash -n scripts/*.sh experiments/runs/*.sh experiments/lib/*.sh
scripts/check_repo_format.sh
scripts/check_markdown_links.py
scripts/check_js_syntax.sh
CXXFLAGS_EXTRA="-Wall -Wextra -Wpedantic" scripts/build.sh all
./build/did_demo_zk --help
scripts/run_cpp_tests.sh
scripts/dev.sh smoke
```

See `docs/TESTING.md` for the full local and CI check matrix.

## Coding Guidelines

- Keep shared C++ helpers in `cpp/text_utils.*`, `cpp/json_utils.*`, and `cpp/process_utils.*`.
- Prefer argv-based process execution through `process_utils` over shell string construction.
- Keep scripts as orchestration wrappers; put stable behavior in C++ or small reusable helpers.
- Do not commit generated runtime outputs from `build/`, `run/`, `logs/`, `results/`, `zk_inputs/`, or `zk_proofs/`.
- Keep configuration changes documented in `docs/CONFIGURATION.md`.
- Keep experiment runner changes documented in `docs/EXPERIMENTS.md` and `experiments/README.md`.

## Security Notes

This repository is a local E2E demo and research harness. Do not use demo tokens, demo keys, or local Hardhat accounts in production systems.
