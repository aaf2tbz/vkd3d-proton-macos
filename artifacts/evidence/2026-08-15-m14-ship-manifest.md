# M14 ship artifact manifest

## artifact
artifacts/VKD3D-Proton-MacOS-2026-08-15.tar.zst
- sha256: 8fbb6924793d3266cc3fc0d937acaecc8883883e0310a790889f6a0e9bff5866
- size: 508381892 bytes, 15922 entries
- built by: scripts/ship-m14.sh (deterministic tar: uid/gid 0, mtime 0, sorted paths, zstd -19)

## pinned lane hashes (verify-by-redownload)
- artifacts/bin/d3d12.dll 0fc399509bd8ae06f73621a1f55381b8ac79d62011c6b212ce20e5f42c982c3e
- artifacts/bin/d3d12core.dll 1659e641dec64eb4956fd96511a5ac9951d87ea3268e4294586fed6d94c26174
- artifacts/bin/libMoltenVK.dylib 2e25de795e7df5e4bbd1715d9f39d54569ed5ac6ad31314c05204ab3ad35df43

## contents
- workspace committed state (README, ROADMAP, docs/, scripts/, artifacts/evidence/)
- fork trees with git histories: sources/{MoltenVK,vkd3d-proton,SPIRV-Cross,MetalSharp}/.git
- built binaries under artifacts/bin/ + the repackaged metalsharp-graphics-dll-m12.tar.zst
- BUILD.md (toolchain, build steps, launch env, verification)

## publish (user action)
- create the public VKD3D-Proton-MacOS repo on GitHub, push all commits
- attach this artifact to a release (or commit it with git-lfs)
- verify-by-redownload: re-download, re-extract, re-hash the three pins above
