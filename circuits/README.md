# Circom circuits for DID ZK anonymous authentication

Files:
- `common/poseidon_merkle.circom`: reusable components
- `state_leaf_check.circom`: unit-test circuit for `cid` and `leaf`
- `auth_membership.circom`: main anonymous membership circuit

Document-aligned formulas:
- `cid = Poseidon(sid, rho)`
- `leaf = Poseidon(cid, pkNormHash, pkRecHash, ver, active)`
- `bindHash = Poseidon(ctxHash, sessPkHash, epoch)`
- `nullifier = Poseidon(cid, ctxHash, epoch)`

Notes:
- `bindHash` is kept only as a session-binding / debugging helper.
- `sessPkHash` remains a public input of `AuthMembership`, so the proof statement is still bound to the session public key.
- `nullifier` is restored to the experiment-document definition and no longer absorbs `sessPkHash` through `bindHash`.
- `leaf` and `bindHash` can remain public outputs for debugging during experiments; if you later want a stricter production-style interface, you can remove one or both after updating scripts and verifiers.

Public inputs of `auth_membership.circom`:
- `root`
- `ctxHash`
- `sessPkHash`
- `epoch`

Private witness of `auth_membership.circom`:
- `sid`
- `rho`
- `pkNormHash`
- `pkRecHash`
- `ver`
- `pathElements[depth]`
- `pathIndex[depth]`

Current public outputs of `auth_membership.circom`:
- `nullifier`
- `bindHash`
- `leaf`

Output semantics:
- The current implementation exposes `nullifier`, `bindHash`, and `leaf` as public outputs.
- `bindHash` is retained only for debugging / transcript inspection.
- If `bindHash` is removed in a later cleanup, you must keep an explicit constraint path that still binds `sessPkHash` to the proof statement. In other words, deleting `bindHash` is safe only if `sessPkHash` continues to be constrained by some other circuit-visible mechanism and the verifier / transcript logic is updated accordingly.

Recommended debug order:
1. Compile and test `state_leaf_check.circom`
2. Verify external code computes identical `cid` and `leaf`
3. Compile and test `auth_membership.circom`
4. Feed real `root` and Merkle path from `bb_service_zk.js`
5. Check `public.json` ordering before wiring any C++ / JS verifier logic

Quick reminders:
- `StateLeafCheck` should stay the first acceptance gate for leaf consistency.
- If you previously assumed `nullifier = Poseidon(cid, bindHash)` in scripts, demos, or C++ code, update them all together.
- After this change, regenerate `r1cs`, `zkey`, `vk`, `proof.json`, and `public.json`.
