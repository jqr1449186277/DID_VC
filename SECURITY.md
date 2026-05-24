# Security Policy

## Supported Versions

DID E2E is a research prototype and local demonstration harness. The `main` branch is the only supported branch for security fixes.

## Reporting a Vulnerability

Please do not open a public issue for a suspected vulnerability. Report it privately to the project maintainers through the repository owner's preferred private channel. Include:

- affected commit or release
- reproduction steps
- expected and actual impact
- relevant logs, inputs, or proof artifacts

If the repository has no private reporting channel configured yet, contact the maintainer before publishing details.

## Security Scope

In scope:

- key, recovery, TTSS share, and trace-flow logic
- ZK witness/proof handling and verifier integration
- smart contract authorization checks
- API behavior that can corrupt identity state or leak secrets

Out of scope:

- local Hardhat accounts and demo private keys
- demo bearer tokens
- generated experiment outputs
- denial-of-service issues against the local development stack

## Production Warning

This project is not production software. It uses local development services, demo tokens, local-chain accounts, and experimental cryptographic flows. Do not use it to protect real identities, credentials, funds, production keys, or regulated personal data.
