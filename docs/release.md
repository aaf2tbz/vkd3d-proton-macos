# Runtime release archive

The public runtime is distributed through the GitHub release asset
[`vkd3d-proton-macos.tar.zst`](https://github.com/metalsharp/vkd3d-proton-macos/releases/tag/v1.0).

## Archive contents

```text
vkd3d-proton-macos/
├── dxgi.dll
├── d3d12.dll
├── d3d12core.dll
├── libMoltenVK.dylib
├── MoltenVK_icd.json
├── README.md
└── SHA256SUMS
```

`dxgi.dll` is the native x86_64 DXGI provider built from the pinned
DXVK-macOS lane and is bundled with the matched D3D12 runtime. The DLLs are
x86_64 PE files for Wine/Rosetta. MoltenVK is universal
x86_64/arm64 and adhoc codesigned for the isolated runtime override path.

## macOS 14 / Metal 3 compatibility

The bundled `libMoltenVK.dylib` is built with the current Xcode beta and
`MACOSX_DEPLOYMENT_TARGET=14.0`. Its Mach-O `LC_BUILD_VERSION` reports
`minos 14.0` for both architectures, so the native runtime has a macOS 14
deployment floor. The D3D12 DLLs remain x86_64 Windows binaries and run
through a compatible Wine/Rosetta environment.

The packaged candidate passed the complete regression suite on Apple GPU.
That host is newer than macOS 14/Metal 3; functional execution specifically
on Metal 3 still requires a macOS 14 host.

## Install/use

Extract the archive and keep the ICD manifest beside the dylib:

```bash
tar --use-compress-program=zstd -xf vkd3d-proton-macos.tar.zst
cd vkd3d-proton-macos
shasum -a 256 -c SHA256SUMS
```

Stage `dxgi.dll`, `d3d12.dll`, and `d3d12core.dll` in the Wine application or
runtime DLL directory. Launch with the MoltenVK directory in the
dynamic-library path:

```bash
export WINEDLLOVERRIDES="d3d12,d3d12core,dxgi=n,b"
export DYLD_LIBRARY_PATH="$PWD"
export VK_ICD_FILENAMES="$PWD/MoltenVK_icd.json"
```

The package is a runtime artifact, not a complete source/build environment.
To rebuild it, clone this repository, install
[requirements.md](requirements.md), and use the root Makefile.

## Publishing

After the validation gate passes:

```bash
make package PACKAGE=./vkd3d-proton-macos.tar.zst
gh release upload v1.0 ./vkd3d-proton-macos.tar.zst --clobber
```

Do not commit generated release archives or the large local source/build
trees. Keep them as GitHub release assets.
