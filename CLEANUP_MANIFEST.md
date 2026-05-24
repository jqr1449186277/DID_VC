# did-e2e Cleanup Manifest

Generated from inspection of `/home/spike/did-e2e`.

This manifest is intentionally conservative. Items under "Archive" should be moved to `_archive/` first, not deleted immediately. Items under "Rebuildable" can be regenerated, but some are expensive or convenient to keep.

## Keep

Core Solidity / Hardhat implementation:

- `hardhat/contracts/DIDBulletinBoardZK.sol` - on-chain DID record, active root, TTSS metadata, and trace anchor contract.
- `hardhat/scripts/deploy_zk.js` - deploys `DIDBulletinBoardZK`.
- `hardhat/hardhat.config.js` - Hardhat network/compiler config.
- `hardhat/package.json`
- `hardhat/package-lock.json`
- `hardhat/bb_service_zk.js` - main bulletin-board service, chain mirror, sparse Poseidon-Merkle tree, root updater, TTSS and trace endpoints.
- `hardhat/pirate_box.js` - stateless reconstruction-box simulation for trace experiments.
- `hardhat/tracer_client.js` - trace-run, trace-verify, and publish-trace CLI.
- `hardhat/scripts/fault_rules.sh` - keep if chain/communication fault experiments are still used.
- `hardhat/scripts/mining_mode.sh` - keep if Hardhat mining-mode experiments are still used.
- `hardhat/bb_set_contract.sh` - small helper; keep until startup flow is simplified.

Core C++ implementation:

- `cpp/main_submitonly_waitactive_zk.cpp` - current primary executable source for ZK auth, recovery, TTSS setup/recover/trace flows.
- `cpp/committee_node.cpp` - guardian/committee HTTP node.
- `cpp/httplib.h` - vendored HTTP server/client header used by committee and client code.
- `cpp/merkle_poseidon.cpp`
- `cpp/merkle_poseidon.hpp`
- `cpp/zk_backend.cpp`
- `cpp/zk_backend.hpp`
- `cpp/verifier_wrap.cpp`
- `cpp/verifier_wrap.hpp`
- `cpp/input_export.cpp`
- `cpp/share_envelope.cpp`
- `cpp/share_envelope.hpp`
- `cpp/ttss_nits_shamir.cpp`
- `cpp/ttss_nits_shamir.hpp`
- `cpp/ttss_trace.cpp`
- `cpp/ttss_trace.hpp`

Useful C++ tests / fixtures:

- `tests/cpp/tests_ttss_share_rec.cpp`
- `tests/cpp/tests_ttss_trace.cpp`
- `tests/cpp/test_poseidon_native.cpp`
- `tests/cpp/gen_ttss_fixture.cpp`

Circom source:

- `circuits/auth_membership.circom` - current main anonymous-membership circuit.
- `circuits/state_leaf_check.circom` - leaf/cid unit-test circuit.
- `circuits/common/poseidon_merkle.circom` - reusable Poseidon-Merkle components.
- `circuits/circuits_README_synced (2).md` - documents current public input/output semantics.

Primary scripts:

- `scripts/build.sh` - current C++ build script. This links `-lsodium -lgmpxx -lgmp -pthread`.
- `scripts/start_stop.sh` - current environment orchestration script.
- `scripts/setup_groth16.sh` - Groth16 setup script. Note: it references missing `scripts/compile_zk.sh`.
- `scripts/run_state_leaf_check.sh`
- `experiments/tests/test_poseidon_native.sh`
- `scripts/run_G_zk_auth.sh`
- `scripts/run_H_zk_recovery.sh`
- `scripts/run_H_zk_recovery_batch.sh`
- `scripts/run_I_zk_scale.sh`
- `scripts/run_J_ttss_recover_batch.sh`
- `scripts/run_J_ttss_threshold_boundary.sh`
- `scripts/run_K_ttss_trace_cases.sh`
- `scripts/run_K_ttss_trace_batch.sh`
- `scripts/run_L_ttss_scale.sh`
- `scripts/run_M_ttss_nt_matrix.sh`
- `scripts/run_M_ttss_nt_matrix_failstage.sh`
- `scripts/run_M_ttss_nt_matrix_failstage_phased.sh`
- `scripts/run_M_chain_comm_suite.sh`
- `scripts/run_N_zk_depth_scale.sh`
- `scripts/run_O_activate_depths_and_test.sh`
- `scripts/run_JKL_suite.sh`
- `scripts/common_chain_comm.sh`
- `scripts/http_byte_proxy.py`
- `scripts/gas_monitor.js`
- `scripts/zk_verify_service.js`
- `scripts/gen_vectors.mjs`
- `scripts/leaf_utils.mjs`
- `scripts/prepare_auth_input.mjs`
- `scripts/summarize_chain_comm.py`
- `experiments/tools/summarize_ttss_results.py`
- `scripts/did_generate_experiment_figures.py`
- `scripts/analyze_depth_nt_results*.py` and figure scripts under `results/` should be moved into `scripts/analysis/` if they are still used.

Published / summarized result artifacts to keep:

- `results/depth_nt_analysis/`
- `results/baseline_section_output/`
- `results/exp34_horizontal_output/`
- `results/20260409_165823_M_chain_comm_suite/`
- Summary CSV files inside older `results/20260409_*_M_chain_comm_suite/` directories if they are cited.

ZK artifacts worth keeping for fast reruns:

- `zk_build/vk/auth_membership_vk.json`
- `zk_build/zkey/auth_membership_final.zkey`
- `zk_build/auth_membership/verification_key.json`
- `zk_build/auth_membership/auth_membership_final.zkey`
- `zk_build/auth_membership/auth_membership.r1cs` if matching the current circuit.
- `zk_build/state_leaf_check/state_leaf_check.r1cs`
- `zk_build/state_leaf_check/state_leaf_check.sym`
- `zk_build/state_leaf_check/state_leaf_check_cpp/` if present.

## Archive

Create `_archive/` and move these there first. Keep them for one cleanup cycle, then delete only after confirming no scripts reference them.

Historical C++ entrypoints / snapshots:

- `cpp/main01.cpp`
- `cpp/main02.cpp`
- `cpp/main03.cpp`
- `cpp/main_submitonly_waitactive_zk_OK1000.cpp`
- `cpp/check.cpp`
- `cpp/test_link_zk.cpp`

Reason: `scripts/build.sh` builds from `cpp/main_submitonly_waitactive_zk.cpp`, not these historical files.

Duplicate or dated scripts:

- `scripts/build01.sh` - old build script; differs by missing `-lgmpxx -lgmp`.
- `scripts/start_stop01.sh` - same hash as `start_stop02.sh`, older copy.
- `scripts/start_stop02.sh` - duplicate of `start_stop01.sh`.
- `scripts/start_stop_n10.sh` - variant for 10 committee nodes; archive unless actively used.
- `scripts/run_G_zk_auth01.sh`
- `scripts/run_O_activate_depths_and_test01.sh`

Circuit backups / variants:

- `circuits/auth_membership.circom.bak` - exact same hash as `circuits/auth_membership.circom`.
- `circuits/auth_membership_base.circom` - older/base variant; archive unless needed as reference.

Old runtime state and logs:

- `hardhat/npminstall-debug.log`
- `logs/*.log`
- `results/_logs/*.log`
- `run/*.pid`
- `run/ttss_phase5_smoke_last.json`

Note: current `bb_service_zk.pid` and `hardhat-node.pid` were stale during inspection. Do not trust PID files after service restarts.

Zip archives duplicated by extracted/summarized results:

- `results/baseline_100.zip`
- `results/TreeDeepth_16_20.zip`
- `results/ttss_nt.zip`
- `results/exp_34.zip`
- `results/20260409_165823_M_chain_comm_suite.zip`

Archive these to external storage if the extracted summaries and figures are the canonical outputs.

Old or intermediate chain-communication suites:

- `results/20260409_163512_M_chain_comm_suite/`
- `results/20260409_163738_M_chain_comm_suite/`
- `results/20260409_164300_M_chain_comm_suite/`
- `results/20260409_164509_M_chain_comm_suite/`
- `results/20260409_165029_M_chain_comm_suite/`

Keep only their summary CSVs if cited; otherwise archive the full directories.

Smoke-test leftovers:

- `results/_smoke_phase5_env_env_smoke_1775724289/`
- `results/_smoke_phase5_env_env_smoke_1775724624/`
- `results/_smoke_phase5_env_env_smoke_1775725092/`

## Rebuildable

These are generated or dependency directories. They should be ignored by version control. Keep locally only for convenience or expensive regeneration.

Build products:

- `build/did_demo_zk`
- `build/committee_node`
- `build/*.o`

Regenerate with:

```bash
bash scripts/build.sh all
```

Hardhat generated files:

- `hardhat/artifacts/`
- `hardhat/cache/`

Regenerate with Hardhat compile/deploy flows.

Node dependencies:

- `hardhat/node_modules/` - rebuild with `cd hardhat && npm install`.
- `node_modules/` - root has no `package.json`; likely accidental dependency tree. Remove after confirming no command relies on root-level `node_modules/.bin`.
- `circuits/node_modules/` - likely installed for Circom/snarkjs/circomlib include convenience. Prefer documenting install command and removing from source tree if global tools are available.

ZK build products:

- `zk_build/auth_membership_d16/`
- `zk_build/auth_membership_d17/`
- `zk_build/auth_membership_d18/`
- `zk_build/auth_membership_d19/`
- `zk_build/auth_membership_d20/`
- `zk_build/state_leaf_check/`
- `zk_build/auth_membership/`
- `zk_inputs/`
- `zk_proofs/`

Keep current `vk` and final `zkey` if fast reruns are needed; otherwise regenerate through Circom/snarkjs/rapidsnark setup.

Powers of tau:

- `zk_build/ptau/pot20_final.ptau` - expensive but reusable.
- `zk_build/ptau/pot20_0000.ptau`
- `zk_build/ptau/pot20_0001.ptau`
- `zk_build/ptau/pot14_0000.ptau`
- `zk_build/ptau/pot14_0001.ptau`
- `zk_build/ptau/pot14_final.ptau`

Suggested rule: keep only the final `.ptau` files locally; move intermediate contribution files to external artifact storage.

Runtime state:

- `run/`
- `logs/`
- `results/_logs/`
- `hardhat/bb_state.json`
- `hardhat/bb_state_zk.json`
- `hardhat/.bb_state.json`

These are useful for debugging a current run but should not be treated as source.

## Needs Confirmation

Items that may be useful but need owner confirmation before moving or deleting:

- `hardhat/bb_state_zk.json` - large state snapshot, may contain latest service mirror / operation cache.
- `hardhat/bb_state.json` and `hardhat/.bb_state.json` - older state formats or compatibility snapshots.
- `run/ttss_phase5_env.sh`
- `run/zk_chain_comm_env.sh`
- `run/zk_local_env.sh`

These may encode the working local environment and should be preserved until startup is verified from clean scripts.

- `scripts/start_stop_n10.sh` - keep if 10-committee experiments are still active.
- `scripts/run_M_ttss_nt_matrix_failstage*.sh` - keep if failure-stage analysis is part of the paper/report.
- `results/20260409_*_M_chain_comm_suite/` - decide which run is canonical.
- `results/*.zip` - decide whether zip files or extracted directories are canonical.
- `zk_build/ptau/pot20_final.ptau` - keep if the environment must rerun Groth16 setup without downloading/regenerating.
- `tests/cpp/gen_ttss_fixture.cpp` - keep if used for fixture generation; otherwise archive with tests.
- `hardhat/scripts/fault_rules.sh` and `hardhat/scripts/mining_mode.sh` - keep only if chain fault / mining-mode experiments are still used.

## Suggested Target Layout

```text
did-e2e/
  cpp/                    # C++ source and tests only
  hardhat/                # package.json, contracts, services, deploy scripts
  circuits/               # Circom source and README only
  scripts/                # primary scripts
  scripts/analysis/       # result analysis / figure generation scripts
  docs/                   # design PDFs and project notes
  build/                  # generated binaries and object files
  zk_build/               # generated ZK artifacts
  results/                # experiment outputs
  run/                    # live runtime state
  logs/                   # live logs
  _archive/               # historical source snapshots and deprecated scripts
```

## Suggested `.gitignore`

```gitignore
build/
logs/
run/*.pid
run/*_last.json
results/_logs/
results/_smoke*/
*.log
*.o

node_modules/
hardhat/node_modules/
circuits/node_modules/
hardhat/artifacts/
hardhat/cache/
hardhat/bb_state*.json
hardhat/.bb_state.json
hardhat/npminstall-debug.log

zk_inputs/
zk_proofs/
zk_build/**/witness.wtns
zk_build/**/proof.json
zk_build/**/public.json
zk_build/ptau/*_0000.ptau
zk_build/ptau/*_0001.ptau
```

Do not ignore final published result summaries or final verification keys unless an external artifact store is used.

## Cleanup Order

1. Initialize git or create a full tarball backup before moving anything.
2. Create `_archive/`.
3. Move the "Archive" section files/directories into `_archive/`.
4. Verify build:

```bash
bash scripts/build.sh all
```

5. Verify services from a clean start:

```bash
bash scripts/start_stop.sh down
bash scripts/start_stop.sh up
```

6. Verify one fast smoke path, for example state leaf or one TTSS setup.
7. Remove or externalize rebuildable bulky artifacts only after smoke tests pass.
