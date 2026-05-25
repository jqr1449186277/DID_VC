# Experiments

Experiment scripts live under `experiments/` and are separate from the stable build/dev entrypoints in `scripts/`. Start the native or Docker development stack before running them.

## Maintained Runners

- `experiments/runs/run_G_zk_auth.sh`: repeated ZK authentication runs against the bulletin-board service.
- `experiments/runs/run_H_zk_recovery.sh`: single legal ZK recovery run.
- `experiments/runs/run_J_ttss_recover_batch.sh`: repeated TTSS setup, recover, and recover+rotate runs.
- `experiments/runs/run_J_ttss_threshold_boundary.sh`: threshold behavior for `t-1`, `t`, and `t+1` shares.
- `experiments/runs/run_K_ttss_trace_batch.sh`: repeated trace, verify, and optional trace-publish runs.
- `experiments/runs/run_K_ttss_trace_cases.sh`: no-leak, bad-metadata, and leaked-share case matrix.
- `experiments/runs/run_L_ttss_scale.sh`: TTSS `n/t` scale smoke over the currently configured committee URLs.
- `experiments/runs/run_JKL_suite.sh`: combined J/K/L suite plus TTSS summary generation.
- `experiments/runs/run_M_chain_comm_suite.sh`: chain gas and HTTP byte instrumentation around maintained runners.

## Helpers

- `experiments/lib/common_chain_comm.sh`: shared chain/communication instrumentation helpers.
- `experiments/lib/gas_monitor.js`: local chain transaction/gas monitor.
- `experiments/lib/http_byte_proxy.py`: HTTP proxy that records message counts and byte totals.
- `experiments/tools/summarize_ttss_results.py`: TTSS result summarizer.
- `experiments/tools/summarize_chain_comm.py`: chain/communication result summarizer.

## Common Usage

```bash
scripts/dev.sh up
bash experiments/runs/run_G_zk_auth.sh
bash experiments/runs/run_H_zk_recovery.sh
bash experiments/runs/run_J_ttss_recover_batch.sh
bash experiments/runs/run_K_ttss_trace_cases.sh
scripts/dev.sh down
```

Most runners write to `results/<timestamp>_<experiment_name>/` unless `OUT` or `OUT_DIR` is provided.

## Useful Smoke Overrides

Use small counts while checking script compatibility:

```bash
RUNS_BB1=1 RUNS_BB0=0 OUT=results/script_check_G bash experiments/runs/run_G_zk_auth.sh
OUT=results/script_check_H bash experiments/runs/run_H_zk_recovery.sh
RUNS=1 OUT_DIR=results/script_check_J bash experiments/runs/run_J_ttss_recover_batch.sh
RUNS=1 OUT_DIR=results/script_check_K bash experiments/runs/run_K_ttss_trace_batch.sh
```

For chain/communication instrumentation:

```bash
MODE=G RUNS_BB1=1 RUNS_BB0=0 OUT_DIR=results/script_check_M_G bash experiments/runs/run_M_chain_comm_suite.sh
```

## Removed Legacy Categories

Legacy multi-tree-depth activation/sweep scripts, `state_leaf_check` probes, standalone Poseidon fixture shell tests, and hard-coded figure builders were removed from the open-source tree. They depended on historical generated artifacts or old result directories and did not match the current default project layout.

Different tree depths require regenerating compatible circuit assets and should be treated as a separate experiment setup, not as part of the default smoke suite.
