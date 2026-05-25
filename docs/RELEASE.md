# Release Process

DID E2E has not published a stable release yet. The first intended public tag is `v0.1.0`.

## Versioning

Use semantic-version-style tags:

- `v0.x.y` for experimental prototype releases.
- Breaking changes are allowed in `0.x` releases but should still be documented.
- Do not claim production readiness while the project remains unaudited.

## v0.1.0 Release Checklist

- [ ] `git status --short` is clean before tagging.
- [ ] `CHANGELOG.md` has an entry for `v0.1.0`.
- [ ] `README.md` clearly states experimental/non-production status.
- [ ] `docs/DID_METHOD.md` lists unsupported W3C DID/VC functionality.
- [ ] `docs/THREAT_MODEL.md` reflects the current trust assumptions.
- [ ] `docs/DEPENDENCIES.md` reflects current native, npm, and ZK dependencies.
- [ ] `docs/TESTING.md` matches CI.
- [ ] `scripts/build.sh all` passes locally.
- [ ] `scripts/run_cpp_tests.sh` passes locally.
- [ ] `scripts/dev.sh up`, `scripts/dev.sh smoke`, and `scripts/dev.sh down` pass locally.
- [ ] GitHub Actions pass on the release commit.
- [ ] Generated outputs under `build/`, `run/`, `results/`, `zk_build/`, `zk_inputs/`, and `zk_proofs/` are not committed.
- [ ] Tag `v0.1.0` points at the intended commit.

## Suggested Commands

```bash
git status --short
bash -n scripts/*.sh experiments/runs/*.sh experiments/lib/*.sh
scripts/check_repo_format.sh
scripts/check_markdown_links.py
scripts/check_js_syntax.sh
CXXFLAGS_EXTRA="-Wall -Wextra -Wpedantic" scripts/build.sh all
scripts/run_cpp_tests.sh
scripts/dev.sh up
scripts/dev.sh smoke
scripts/dev.sh down
```

## Tagging

```bash
git tag -a v0.1.0 -m "v0.1.0"
git push origin main
git push origin v0.1.0
```

## Release Notes

Release notes should include:

- project status and production warning
- supported flows
- unsupported W3C DID/VC features
- test and smoke coverage
- known limitations
- dependency and trusted-setup notes
