# Experiments

This directory contains research and benchmarking scripts. The stable public entrypoints remain in `scripts/`:

- `scripts/build.sh`
- `scripts/dev.sh`
- `scripts/start_stop.sh`
- `scripts/run_cpp_tests.sh`

Use the scripts here only when you want to reproduce or extend a specific experiment batch. The fuller operator guide is `docs/EXPERIMENTS.md`.

## Layout

- `runs/`: maintained batch experiments and scenario suites.
- `lib/`: experiment-only helpers for chain gas and HTTP byte instrumentation.
- `tools/`: maintained CSV/JSON summarizers for experiment outputs.

## Maintained Runners

- `runs/run_G_zk_auth.sh`: repeated ZK authentication runs against the BB service.
- `runs/run_H_zk_recovery.sh`: single legal ZK recovery run.
- `runs/run_J_ttss_recover_batch.sh`: repeated TTSS setup, recover, and recover+rotate runs.
- `runs/run_J_ttss_threshold_boundary.sh`: TTSS threshold boundary checks for `t-1`, `t`, and `t+1` shares.
- `runs/run_K_ttss_trace_batch.sh`: repeated TTSS trace, verify, and optional publish runs.
- `runs/run_K_ttss_trace_cases.sh`: no-leak, bad-metadata, and leak trace cases.
- `runs/run_L_ttss_scale.sh`: TTSS `n/t` scale runs over the currently configured committee URLs.
- `runs/run_JKL_suite.sh`: combined J/K/L suite plus TTSS summary generation.
- `runs/run_M_chain_comm_suite.sh`: chain and communication instrumentation suite around the maintained runners.

## Common Usage

Start the dev stack first:

```bash
scripts/dev.sh up
```

Run one experiment, for example:

```bash
bash experiments/runs/run_G_zk_auth.sh
bash experiments/runs/run_H_zk_recovery.sh
bash experiments/runs/run_J_ttss_recover_batch.sh
bash experiments/runs/run_K_ttss_trace_cases.sh
```

Experiment scripts default to paths relative to the repository root, so they can be run from the root directory without setting `PROJECT_ROOT`.

## Removed Legacy Scripts

The old multi-tree-depth activation/sweep scripts, `state_leaf_check` probes, standalone Poseidon fixture shell test, and hard-coded figure builders were removed from the open-source tree. They depended on legacy generated artifacts or historical result directories and were not adapted to the current default project layout.

Current C++ validation lives in `scripts/run_cpp_tests.sh`; experiment result summarizers live in `experiments/tools/`.
