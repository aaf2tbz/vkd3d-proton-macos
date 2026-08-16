# Runtime release archive

The public runtime is distributed through the GitHub release asset
[`vkd3d-proton-macos.tar.zst`](https://github.com/aaf2tbz/vkd3d-proton-macos/releases/tag/m14).

## Archive contents

```text
vkd3d-proton-macos/
├── d3d12.dll
├── d3d12core.dll
├── libMoltenVK.dylib
├── MoltenVK_icd.json
├── README.md
└── SHA256SUMS
```

The DLLs are x86_64 PE files for Wine/Rosetta. MoltenVK is universal
x86_64/arm64 and adhoc codesigned for the isolated runtime override path.

## Install/use

Extract the archive and keep the ICD manifest beside the dylib:

```bash
tar --use-compress-program=zstd -xf vkd3d-proton-macos.tar.zst
cd vkd3d-proton-macos
shasum -a 256 -c SHA256SUMS
```

Stage `d3d12.dll` and `d3d12core.dll` in the Wine application/runtime DLL
directory. Launch with the MoltenVK directory in the dynamic-library path:

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
gh release upload m14 ./vkd3d-proton-macos.tar.zst --clobber
```

Do not commit generated release archives or the large local source/build
trees. Keep them as GitHub release assets.
