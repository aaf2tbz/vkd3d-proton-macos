# Build guide

The root `Makefile` is the supported interface. Run `make help` for the
short list; the targets below show their dependencies and outputs.

## 1. Verify the workspace

```bash
make docs-check
make tools
```

`make tools` is intentionally strict about the validated Xcode, llvm-mingw,
Wine, and source-clone paths. On another host, adjust `scripts/env.sh` first.

## 2. Build each runtime component

```bash
make vkd3d       # artifacts/build/vkd3d-proton/x86_64-windows/*.dll
make moltenvk    # artifacts/build/moltenvk-vkmt.build.new/* (staged only)
make build       # both components
```

The vkd3d build uses Meson/Ninja and the llvm-mingw cross file generated at
`artifacts/vkd3d-cross-x86_64.txt`. The MoltenVK build fetches its pinned
externals, reapplies `scripts/patch-spirv-cross.sh`, and writes a candidate
to `artifacts/build/moltenvk-vkmt.build.new/`. Promotion is deliberately
manual: run the validation gate first, then copy the candidate into
`artifacts/build/moltenvk-vkmt/`.

## 3. Build and stage probes

```bash
make flprobe
make stage
```

`make flprobe` creates `artifacts/stage-dxr/flprobe.exe`. The other D3D12
probes use precompiled DXIL blobs produced by `dxc`; see
[validation.md](validation.md) for the shader and staging layout.

## 4. Run the ladder

The Wine runner is environment-specific. The historical validation runner is
`/tmp/run-probe.sh`; provide another runner with `WINE_RUNNER`:

```bash
make ladder WINE_RUNNER=/tmp/run-probe.sh
```

The expected result is six successful device creations (`12_2` through
`1_0_CORE`) and `max=12_2`. Never accept a ladder result without checking the
loaded module paths and the hashes of the staged DLLs.

## 5. Package the runtime

After the validated runtime has been promoted to the canonical staging
directories:

```bash
make package PACKAGE=./vkd3d-proton-macos.tar.zst
```

The package target includes the two DLLs, universal MoltenVK, ICD manifest,
README, and SHA256SUMS. Large/generated archives are release assets, not Git
files.

## Target map

| Target | Result |
|---|---|
| `help` | Show targets |
| `tools` | Validate host/toolchain/source prerequisites |
| `docs-check` | Validate required docs, badges, and whitespace |
| `env` | Load and print the workspace environment |
| `vkd3d` | Build D3D12 DLL pair |
| `moltenvk` | Build candidate universal MoltenVK |
| `build` | Run both component builds |
| `flprobe` | Build feature-level probe |
| `stage` | Stage matched runtime files |
| `ladder` | Run `flprobe.exe` under the configured Wine runner |
| `package` | Create deterministic zstd runtime archive |
| `test` | Run portable documentation/interface checks |
| `clean` | Remove generated build configuration/output only |
