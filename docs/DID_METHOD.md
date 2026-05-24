# DID Method Notes

This repository does not currently define a registered W3C DID method. It implements the core state and authentication mechanisms that could back a future DID method.

## Experimental Method Shape

A future method could use a DID of the form:

```text
did:didzk:<idHash>
```

Where `<idHash>` is the 32-byte identity hash tracked by the bulletin board contract.

## Current State Model

The local chain stores an identity record keyed by `idHash`:

- `cid` - commitment / DID pointer used by the ZK leaf
- `owner` - normal controller address
- `recovery` - recovery controller address
- `version` - monotonic identity version
- `active` - whether the record participates in the active root
- `pkNormHash` - normal public-key hash in field form
- `pkRecHash` - recovery public-key hash in field form

The bulletin board service maintains an off-chain Poseidon-Merkle mirror and anchors the active root and epoch on-chain.

## Create, Read, Update, Recover

Potential DID-method mapping:

- Create: `registerZK`
- Read: `getRecord`, `/record`, `/leaf`, `/path`, `/root`
- Update: `applyUpdateZK`
- Recover: `applyRecoveryRotateZK`
- Attach TTSS metadata: `setTTSSMeta`
- Publish trace anchor: `publishTraceResult`

Deactivation is not yet implemented as a public method operation.

## DID Document Projection

A resolver layer should project the current record into a DID Document with:

- `id`
- `controller`
- `verificationMethod`
- `authentication`
- `assertionMethod`
- optional `service`
- DID document metadata such as `versionId`, `updated`, and `deactivated`

The current code does not yet expose this W3C DID Document projection.

## Not Yet Implemented

- formal DID method specification
- DID resolver and DID URL dereferencing
- W3C-conformant DID Document output
- deactivation operation and metadata
- verification method material beyond internal key hashes
- service endpoint privacy policy
- DID method registration

## Relationship to ZK Authentication

The ZK circuit proves membership in the active identity set without revealing the private identity secret. Public inputs include the root, context hash, session public-key hash, and epoch. Public outputs currently include the nullifier, bind hash, and leaf for experiment visibility.

This is an authentication mechanism that can support a DID method, but it is not by itself a complete W3C DID method.
