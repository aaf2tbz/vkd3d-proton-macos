# Documentation

This directory contains the current build/release guides and the dated
engineering evidence that backs the M14 runtime.

## Start here

| Guide | Purpose |
|---|---|
| [requirements.md](requirements.md) | Host, compiler, SDK, Wine, shader, and release prerequisites |
| [Development.md](Development.md) | Prerequisites, setup, Make targets, builds, debugging, and release workflow |
| [features.md](features.md) | D3D12 feature-level ladder and validated capability matrix |
| [validation.md](validation.md) | Probe staging, runtime environment, and regression gate |
| [release.md](release.md) | Runtime archive contents and installation instructions |

## Engineering records

- [01-feature-level-evidence.md](01-feature-level-evidence.md) — baseline and
  dated feature evidence
- [02-build-toolchain.md](02-build-toolchain.md) — detailed toolchain notes
- [03-validation-harnesses.md](03-validation-harnesses.md) — probe design
- [07-followup-roadmap.md](07-followup-roadmap.md) — historical follow-up work
- [08-remaining-plan.md](08-remaining-plan.md) — historical closure plan
- [09-mesh-samplerfeedback-plan.md](09-mesh-samplerfeedback-plan.md) — mesh and
  sampler-feedback implementation record

The dated files preserve provenance. When a dated note conflicts with the
current guides, the current M14 guides and the evidence files under
`artifacts/evidence/` are authoritative.

## Project policies

- [Contributing](../CONTRIBUTING.md)
- [Security](../SECURITY.md)
- [Code of Conduct](../CODE_OF_CONDUCT.md)
