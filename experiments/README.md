# Experiments

This directory contains research and benchmarking scripts. The stable public entrypoints remain in `scripts/`:

- `scripts/build.sh`
- `scripts/dev.sh`
- `scripts/start_stop.sh`

Use the scripts here when you want to reproduce or extend specific experiment batches.

## Layout

- `runs/`: batch experiments and scenario suites.
- `lib/`: experiment-only helpers such as chain/HTTP instrumentation.
- `tools/`: summarizers, report builders, and figure generation scripts.
- `tools/figures/`: legacy figure builders kept as reusable analysis tools.
- `tests/`: focused research validation scripts that are not part of the default CI path.

## Common Usage

Start the dev stack first:

```bash
scripts/dev.sh up
```

Run one experiment, for example:

```bash
bash experiments/runs/run_G_zk_auth.sh
bash experiments/runs/run_J_ttss_recover_batch.sh
bash experiments/runs/run_K_ttss_trace_cases.sh
```

Experiment scripts default to paths relative to the repository root, so they can be run from the root directory without setting `PROJECT_ROOT`.

Focused validation scripts live in `experiments/tests/`. For example:

```bash
bash experiments/tests/test_poseidon_native.sh
SANITIZE=1 bash experiments/tests/test_poseidon_native.sh
```

The TTSS report summarizer is `experiments/tools/summarize_ttss_results.py`. Older fixed/variant copies were folded into this canonical script.

## Notes

These scripts are intentionally more verbose and parameter-heavy than the stable `scripts/dev.sh` interface. They are kept out of `scripts/` so new users see the normal build/run workflow first.
