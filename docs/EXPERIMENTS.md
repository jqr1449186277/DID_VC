# Experiments

Experiment scripts live under `experiments/` and are separate from the stable build/dev entrypoints in `scripts/`. Start the native or Docker development stack before running them.

## Maintained Runners

| Runner | Purpose | Typical output |
| --- | --- | --- |
| `experiments/runs/run_G_zk_auth.sh` | Repeated ZK authentication against the bulletin-board service. | `zk_auth_bb1.csv`, optional `zk_auth_bb0.csv` |
| `experiments/runs/run_H_zk_recovery.sh` | Legal ZK recovery flow: old proof rejected, new proof accepted. | `zk_recovery.csv`, recovery workdir |
| `experiments/runs/run_J_ttss_recover_batch.sh` | TTSS setup, recover, recover+rotate, old-share invalidation, new-share validation. | `summary.csv`, `attempts.csv` |
| `experiments/runs/run_J_ttss_threshold_boundary.sh` | Threshold behavior for `t-1`, `t`, and `t+1` shares. | `attempts.csv`, `summary.txt` |
| `experiments/runs/run_K_ttss_trace_batch.sh` | Repeated trace, verify, and optional trace-publish runs. | `summary.csv`, `attempts.csv` |
| `experiments/runs/run_K_ttss_trace_cases.sh` | No-leak, bad-metadata, and leaked-share case matrix. | `summary.csv`, `attempts.csv` |
| `experiments/runs/run_L_ttss_scale.sh` | TTSS `n/t` scale smoke over currently configured committee URLs. | `aggregate.csv` |
| `experiments/runs/run_JKL_suite.sh` | Combined J/K/L suite plus TTSS summary generation. | suite subdirectories and summary |
| `experiments/runs/run_M_chain_comm_suite.sh` | Chain gas and HTTP byte instrumentation around maintained runners. | `chain_summary.csv`, `comm_summary.csv`, `chain_comm_summary.csv` |

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

## Runner Groups

- ZK group: `G` and `H` validate anonymous authentication and recovery proof behavior.
- TTSS recovery group: `J` validates share recovery, rotation, and threshold boundaries.
- Trace group: `K` validates accused-set behavior across no-leak, bad-metadata, and leak cases.
- Scale group: `L` checks smaller `n/t` combinations that are possible with currently running committee nodes.
- Instrumentation group: `M` wraps maintained runners with gas and HTTP byte monitors.

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

## Interpreting Results

For the maintained smoke checks, a healthy run should show:

- ZK auth: `ok=1`.
- ZK recovery: old proof invalid, new proof valid, `ok=1`.
- TTSS recovery/rotation: `setup_ok=1`, `recover_ok=1`, `rotate_ok=1`, old shares invalidated, new shares active.
- Threshold boundary: `t-1` fails with insufficient shares, `t` and `t+1` recover successfully.
- Trace batch: accused set matches the expected leaked guardian indexes.
- Trace cases: no-leak avoids false accusation, bad metadata is rejected, leak cases identify the expected set.
- Chain/communication: counted transactions/messages have zero fatal rows and zero communication errors.

Large run counts, different tree depths, and broad `n/t` matrices are intentionally separate from the default open-source smoke path because they take longer and require regenerated or expanded runtime resources.
