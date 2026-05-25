# DID Method Notes

This repository does not currently define a registered W3C DID method. It implements identity-state, authentication, recovery, and trace mechanisms that could back a future DID method after a resolver and method specification are added.

The implementation should be read as a DID-like prototype rather than a complete DID ecosystem.

## Experimental Method Shape

A future method could use a DID of the form:

```text
did:didzk:<idHash>
```

Where `<idHash>` is the 32-byte identity hash tracked by the bulletin-board contract.

The current implementation accepts human-readable demo IDs in local APIs and maps them to `idHash` in local service state. A real DID method would need to define deterministic identifier generation, encoding, collision handling, and resolver behavior.

## Current State Model

The contract stores an identity record keyed by `idHash`:

- `cid`: commitment or DID pointer used by the ZK leaf.
- `owner`: normal controller address for regular updates.
- `recovery`: recovery controller address for recovery rotation.
- `version`: monotonic identity version.
- `active`: whether the record participates in the active root.
- `pkNormHash`: normal public-key hash in field form.
- `pkRecHash`: recovery public-key hash in field form.

The bulletin-board service maintains an off-chain Poseidon-Merkle mirror and anchors the active root and epoch on-chain.

The active leaf formula is:

```text
leaf = Poseidon(cid, pkNormHash, pkRecHash, ver, active)
```

## Operation Mapping

A possible future DID method could map current implementation operations like this:

| DID concept | Current implementation |
| --- | --- |
| Create | `registerZK`, `POST /registerZk` |
| Read | contract `records`, `GET /record`, `GET /leaf`, `GET /path`, `GET /root` |
| Update | `applyUpdateZK`, `POST /applyUpdateZk` |
| Recover | `applyRecoveryRotateZK`, `POST /applyRecoveryRotateZk` |
| Attach recovery metadata | `setTTSSMeta`, `POST /registerTTSSMeta` |
| Publish trace anchor | `publishTraceResult`, `POST /publishTrace` |
| Deactivate | not implemented |
| Resolve DID Document | not implemented |

## Create

Create stores a new identity record with version `0`.

Inputs include:

- `idHash`
- `cid`
- normal and recovery key hashes
- normal and recovery controller addresses

The local service derives demo controller addresses from local seeds. A production DID method would need a wallet/controller integration instead of storing demo seeds in local service state.

## Read

Read currently means one of:

- on-chain `records(idHash)`
- bulletin-board `/record`
- bulletin-board `/leaf`
- bulletin-board `/path`
- bulletin-board `/root`

This is enough for experiments and proof generation, but it is not a W3C resolver. There is no DID URL dereferencing and no DID Document output.

## Update

Regular update requires the normal controller. It changes:

- `cid`
- `pkNormHash`
- `pkRecHash`
- `version`

It does not change `owner` or `recovery`.

## Recover

Recovery requires the recovery controller and rotates both normal and recovery controllers. It changes:

- `cid`
- `pkNormHash`
- `pkRecHash`
- `owner`
- `recovery`
- `version`

Rotating both controllers is intentional. If only the normal controller were rotated, a leaked recovery controller would still retain future takeover power.

## TTSS Recovery Metadata

The TTSS layer is an experiment-specific recovery mechanism. It stores signed share envelopes across committee nodes and anchors only metadata hashes on chain:

- `vkSetHash`
- `metaHash`
- identity version
- root epoch

The chain does not store raw shares. Committee nodes store share envelopes locally and mark old shares inactive after successful recovery rotation.

## Trace Anchor

Trace experiments can publish:

- `accusedSetHash`
- `proofHash`
- `traceDigest`
- identity version
- root epoch

The anchor records that a trace result was produced for a specific identity state and epoch. It does not implement a full dispute or governance process.

## DID Document Projection

A future resolver could project the current record into a DID Document similar to:

```json
{
  "@context": ["https://www.w3.org/ns/did/v1"],
  "id": "did:didzk:0x...",
  "controller": "did:didzk:0x...",
  "verificationMethod": [
    {
      "id": "did:didzk:0x...#normal",
      "type": "Multikey",
      "controller": "did:didzk:0x...",
      "publicKeyMultibase": "<derived-or-published-key-material>"
    },
    {
      "id": "did:didzk:0x...#recovery",
      "type": "Multikey",
      "controller": "did:didzk:0x...",
      "publicKeyMultibase": "<derived-or-published-key-material>"
    }
  ],
  "authentication": ["did:didzk:0x...#normal"],
  "assertionMethod": ["did:didzk:0x...#normal"]
}
```

The current implementation cannot emit this document because it stores only internal key hashes and controller addresses, not W3C verification method material.

## Relationship To ZK Authentication

The ZK circuit proves membership in the active identity set without revealing the private identity secret.

Public inputs include:

- active root
- context hash
- session public-key hash
- epoch

Private witness includes:

- `sid`
- `rho`
- normal and recovery key hashes
- version
- Merkle path

Public outputs currently include:

- `nullifier`
- `bindHash`
- `leaf`

The current proof is an authentication mechanism that could support a DID method, but it is not a DID method by itself.

## W3C DID And VC Gaps

Not yet implemented:

- formal DID method specification
- method registration
- DID resolver
- DID URL dereferencing
- W3C-conformant DID Document output
- DID deactivation operation and metadata
- service endpoint privacy policy
- verification method material beyond internal key hashes
- Verifiable Credential issuance
- Verifiable Presentation verification
- credential status
- wallet storage
- production key management and recovery policy

## Design Status

The current project is best described as:

- a local identity-state and proof prototype
- a DID-method design substrate
- an experiment harness for ZK auth, TTSS recovery, and trace anchoring

It should not be described as a complete W3C DID method or VC implementation until the missing resolver, DID Document, deactivation, VC, and governance pieces are added.
