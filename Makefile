SHELL := /bin/bash

ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
LLVM_MINGW ?= $(ROOT)/toolchain/llvm-mingw-20260616-ucrt-macos-universal
BUILD_DIR ?= $(ROOT)/artifacts/build
STAGE_DIR ?= $(ROOT)/artifacts/stage-dxr
WINE_RUNNER ?= /tmp/run-probe.sh
PACKAGE ?= $(ROOT)/vkd3d-proton-macos.tar.zst

.PHONY: help tools docs-check env vkd3d moltenvk build flprobe stage ladder \
	package test clean

help:
	@printf '%s\n' \
	  'make tools       verify required host tools and source clones' \
	  'make docs-check  validate documentation, links, and whitespace' \
	  'make vkd3d       build the x86_64 D3D12 DLL pair' \
	  'make moltenvk    build the universal MoltenVK dylib (staged, not promoted)' \
	  'make build       run both vkd3d and MoltenVK builds' \
	  'make flprobe     compile the D3D12 feature-level probe' \
	  'make stage       stage the matched D3D12 pair and MoltenVK ICD' \
	  'make ladder      run flprobe through WINE_RUNNER' \
	  'make package     create the release runtime tarball' \
	  'make test        run docs-check and the available local checks' \
	  'make clean       remove generated build/staging outputs only'

tools:
	@bash scripts/validate-toolchain.sh

docs-check:
	@test -f README.md
	@test -f LICENSE
	@test -f Makefile
	@test -f CONTRIBUTING.md
	@test -f SECURITY.md
	@test -f CODE_OF_CONDUCT.md
	@for f in docs/README.md docs/requirements.md docs/Development.md docs/features.md docs/validation.md docs/release.md; do \
		test -f "$$f" || { echo "missing $$f"; exit 1; }; \
	done
	@grep -q 'github.com/aaf2tbz/vkd3d-proton-macos' README.md
	@grep -q 'License-MIT' README.md
	@grep -q 'Build' README.md
	@git diff --check
	@echo 'documentation checks: PASS'

env:
	@source scripts/env.sh

vkd3d:
	@bash scripts/build-vkd3d-proton.sh

moltenvk:
	@bash scripts/build-moltenvk.sh

build: vkd3d moltenvk

flprobe:
	@mkdir -p "$(STAGE_DIR)"
	@"$(LLVM_MINGW)/bin/x86_64-w64-mingw32-clang" -O2 scripts/flprobe.c \
		-o "$(STAGE_DIR)/flprobe.exe" -ldxgi -ld3d12
	@file "$(STAGE_DIR)/flprobe.exe"

stage:
	@test -f "$(BUILD_DIR)/vkd3d-proton/x86_64-windows/d3d12.dll"
	@test -f "$(BUILD_DIR)/vkd3d-proton/x86_64-windows/d3d12core.dll"
	@test -f "$(BUILD_DIR)/moltenvk-vkmt/MoltenVK_icd.json"
	@test -f "$(BUILD_DIR)/moltenvk-vkmt/libMoltenVK.dylib"
	@mkdir -p "$(STAGE_DIR)"
	@cp "$(BUILD_DIR)/vkd3d-proton/x86_64-windows/d3d12.dll" "$(STAGE_DIR)/"
	@cp "$(BUILD_DIR)/vkd3d-proton/x86_64-windows/d3d12core.dll" "$(STAGE_DIR)/"
	@cp "$(BUILD_DIR)/moltenvk-vkmt/"{libMoltenVK.dylib,MoltenVK_icd.json} "$(STAGE_DIR)/"
	@echo "staged runtime in $(STAGE_DIR)"

ladder: flprobe
	@test -x "$(WINE_RUNNER)" || { echo "set WINE_RUNNER=/path/to/run-probe.sh"; exit 1; }
	@cd "$(STAGE_DIR)" && "$(WINE_RUNNER)" flprobe.exe

package:
	@bash scripts/package-runtime.sh "$(PACKAGE)"

test: docs-check
	@echo 'static checks: PASS'
	@echo 'runtime probes: run make ladder and see docs/validation.md'

clean:
	@rm -rf "$(ROOT)/artifacts/build/vkd3d-proton-build" \
		"$(ROOT)/artifacts/build/moltenvk-vkmt.build.new" \
		"$(ROOT)/artifacts/vkd3d-cross-x86_64.txt"
