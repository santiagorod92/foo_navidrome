.PHONY: help test test-clean build-win install-win win-test dev-build dev-build-minor dev-build-major dev-build-no-install release ci-build win-vm-setup win-vm-fetch win-vm-install win-vm-test clean

XWIN_SDK ?= $(HOME)/.local/share/xwin/sdk
BUILD_WIN := build-win

help:
	@echo "foo_navidrome — make targets"
	@echo ""
	@echo "  test              fast clang-cl+wine build/run of MediaEnrichmentLogicTests (Linux)"
	@echo "  test-clean        same, forcing a clean recompile"
	@echo ""
	@echo "  build-win         cross-compile Windows x64 component locally (win-build-local.sh)"
	@echo "  install-win       install built DLL into local Wine foobar2000 + package"
	@echo "  win-test          dispatch build-windows.yml on GH runner, install, [--launch]"
	@echo ""
	@echo "  dev-build         macOS: bump patch, xcodebuild Release, install"
	@echo "  dev-build-minor   macOS: bump minor"
	@echo "  dev-build-major   macOS: bump major"
	@echo "  dev-build-no-install  macOS: bump + build only"
	@echo "  release           macOS: bump, build, install, package, gh release create"
	@echo "  ci-build VERSION=x.y.z   hermetic macOS CI build (as used by semantic-release)"
	@echo ""
	@echo "  win-vm-setup      one-time: set up macOS clang-cl/xwin/WTL toolchain"
	@echo "  win-vm-fetch      build the Win11 ARM64 QEMU install ISO"
	@echo "  win-vm-install    unattended-install the QEMU guest"
	@echo "  win-vm-test       cross-build x64 DLL, deploy over SSH, relaunch in guest [--launch]"
	@echo ""
	@echo "  clean             remove local build-win/ artifacts"

# --- Linux fast-path unit tests (see CLAUDE.md > Windows > tests) ---
test:
	mkdir -p $(BUILD_WIN)/tests
	clang-cl --target=x86_64-pc-windows-msvc -fuse-ld=lld-link /std:c++17 /EHsc /MD /GR /W4 /WX /utf-8 \
		-imsvc "$(XWIN_SDK)/crt/include" -imsvc "$(XWIN_SDK)/sdk/include/um" \
		-imsvc "$(XWIN_SDK)/sdk/include/shared" -imsvc "$(XWIN_SDK)/sdk/include/ucrt" \
		Windows/tests/MediaEnrichmentLogicTests.cpp Windows/MediaEnrichmentLogic.cpp \
		/Fe:$(BUILD_WIN)/tests/MediaEnrichmentLogicTests.exe /Fo:$(BUILD_WIN)/tests/ /link \
		"/libpath:$(XWIN_SDK)/crt/lib/x86_64" "/libpath:$(XWIN_SDK)/sdk/lib/um/x86_64" \
		"/libpath:$(XWIN_SDK)/sdk/lib/ucrt/x86_64" advapi32.lib
	wine $(BUILD_WIN)/tests/MediaEnrichmentLogicTests.exe

test-clean:
	rm -rf $(BUILD_WIN)/tests
	$(MAKE) test

# --- Windows component, local cross-compile ---
build-win:
	./scripts/win-build-local.sh

install-win:
	./scripts/install-windows.sh

win-test:
	./scripts/win-test.sh $(ARGS)

# --- macOS dev loop ---
dev-build:
	./scripts/dev-build.sh

dev-build-minor:
	./scripts/dev-build.sh --minor

dev-build-major:
	./scripts/dev-build.sh --major

dev-build-no-install:
	./scripts/dev-build.sh --no-install

release:
	./scripts/dev-build.sh --new-release

ci-build:
	@if [ -z "$(VERSION)" ]; then echo "usage: make ci-build VERSION=x.y.z"; exit 1; fi
	./scripts/ci-build.sh $(VERSION)

# --- Windows-on-macOS VM testing (scripts/win-vm/) ---
win-vm-setup:
	./scripts/win-vm/setup-mac-toolchain.sh

win-vm-fetch:
	./scripts/win-vm/fetch-win11-arm.sh

win-vm-install:
	./scripts/win-vm/win-vm.sh install

win-vm-test:
	./scripts/win-vm/win-vm-test.sh $(ARGS)

clean:
	rm -rf $(BUILD_WIN)
