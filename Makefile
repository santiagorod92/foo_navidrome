.PHONY: help test test-clean mac-test mac-test-clean \
	win-build win-build-patch win-build-minor win-build-major win-build-launch win-install win-test win-logs \
	mac-build mac-build-patch mac-build-minor mac-build-major mac-build-no-install mac-install mac-release mac-ci-build mac-logs \
	win-vm-setup win-vm-fetch win-vm-install win-vm-test \
	mac-vm mac-vm-reinstall mac-vm-setup mac-vm-install mac-vm-install-vnc mac-vm-run mac-vm-run-vnc mac-vm-test mac-vm-ssh \
	mac-vm-vnc mac-vm-snapshot mac-vm-snapshot-export mac-vm-snapshot-import \
	mac-vm-stop mac-vm-rm mac-vm-logs mac-vm-migrate-vnc mac-vm-reinstall-vnc clean

XWIN_SDK ?= $(HOME)/.local/share/xwin/sdk
BUILD_WIN := build-win
BUILD_MAC := build-mac

# Target naming: <os>-<action>. The OS prefix (win- / mac-) is the only thing
# that varies; the action after it means the same on both platforms, wired to
# whichever script performs those steps for that OS.
#   win-*  -> Windows component, cross-compiled on Linux (clang-cl + wine)
#   mac-*  -> native macOS component (xcodebuild)
#   win-vm-*  -> Windows component built + runtime-tested in a VM on macOS
#   mac-vm-*  -> macOS component runtime-tested in a Docker-OSX VM on Linux

help:
	@echo "foo_navidrome — make targets"
	@echo ""
	@echo "  test                  fast clang-cl+wine build/run of MediaEnrichmentLogicTests (Linux)"
	@echo "  test-clean            same, forcing a clean recompile"
	@echo "  mac-test              native clang++ build/run of the SAME test suite (macOS)"
	@echo "  mac-test-clean        same, forcing a clean recompile"
	@echo ""
	@echo "  win-build             cross-compile Windows x64 component locally (win-build-local.sh, no version bump)"
	@echo "  win-build-patch       bump version.txt patch, then cross-compile"
	@echo "  win-build-minor       bump version.txt minor, then cross-compile"
	@echo "  win-build-major       bump version.txt major, then cross-compile"
	@echo "  win-build-launch      same as win-build, then relaunch local Wine foobar2000 to load it"
	@echo "  win-install           install built DLL into local Wine foobar2000 + package"
	@echo "  win-logs              follow the local Wine debug log, colourised (run beside win-build-launch)"
	@echo "  win-test              dispatch build-windows.yml on GH runner, install, [ARGS=--launch]"
	@echo "                        (win-logs / mac-logs share scripts/navidrome-logs.sh — pass ARGS=-a for the whole file)"
	@echo ""
	@echo "  mac-build             bump patch, xcodebuild Release, install locally"
	@echo "  mac-build-patch       alias for mac-build (explicit patch bump)"
	@echo "  mac-build-minor       bump minor instead of patch"
	@echo "  mac-build-major       bump major instead of patch"
	@echo "  mac-build-no-install  bump + build only (skip install)"
	@echo "  mac-install           install an already-built component + package"
	@echo "  mac-release           bump, build, install, package, gh release create"
	@echo "  mac-ci-build VERSION=x.y.z   hermetic macOS CI build (as used by semantic-release)"
	@echo "  mac-logs              follow the local macOS debug log, colourised (run beside mac-build)"
	@echo ""
	@echo "  win-vm-setup          one-time: set up macOS clang-cl/xwin/WTL toolchain"
	@echo "  win-vm-fetch          build the Win11 ARM64 QEMU install ISO"
	@echo "  win-vm-install        unattended-install the QEMU guest"
	@echo "  win-vm-test           cross-build x64 DLL, deploy over SSH, relaunch in guest [ARGS=--launch]"
	@echo ""
	@echo "  mac-vm                ONE COMMAND: create/boot the guest, snapshot, install foobar2000,"
	@echo "                        deploy the component, launch it. First run needs a one-time macOS"
	@echo "                        install click-through over VNC; every run after is hands-off."
	@echo "                        [ARGS='--release --launch' -> forwarded to mac-vm-test]"
	@echo "  mac-vm-reinstall      wipe the container + :snap image, recreate from scratch and boot"
	@echo "                        the VNC installer — one command, no manual docker juggling"
	@echo "  mac-vm-setup          one-time: host packages + kvm/docker checks + pull Docker-OSX image"
	@echo "  mac-vm-install        GUI-install the macOS guest, X11 window (manual, ~30-45 min)"
	@echo "  mac-vm-install-vnc    same but headless + VNC :5901 (needed on Wayland / NVIDIA hosts)"
	@echo "  mac-vm-run            boot the installed macOS guest (detached, X11)"
	@echo "  mac-vm-run-vnc        boot the installed macOS guest (detached, VNC)"
	@echo "  mac-vm-migrate-vnc    stop+snapshot+rm an X11 guest, reinstall from the snapshot as VNC"
	@echo "  mac-vm-reinstall-vnc  same, reusing an existing :snap (skips the slow re-commit)"
	@echo "  mac-vm-vnc            open a VNC viewer on the guest (tigervnc, else remmina)"
	@echo "  mac-vm-snapshot       docker commit the guest disk to <container>:snap"
	@echo "  mac-vm-snapshot-export / -import  [FILE=x.tar.zst]  back the install up / restore it"
	@echo "                        on any machine — reinstall the macOS guest exactly once, ever"
	@echo "  mac-vm-stop / -rm     stop / delete the guest container (rm loses the macOS disk)"
	@echo "  mac-vm-logs           follow the guest's QEMU/boot log"
	@echo "  mac-vm-test           resolve a .fb2k-component, deploy over SSH, re-sign, relaunch [ARGS='--release --launch']"
	@echo "  mac-vm-ssh            shell into the macOS guest"
	@echo ""
	@echo "  clean                 remove local build-win/ artifacts"

# --- Unit tests (tests/MediaEnrichmentLogicTests.cpp + Windows/MediaEnrichmentLogic.cpp) ---
# One source file, per-host toolchain. scripts/run-unit-tests.sh is the single
# source of truth for the compile command; the local build scripts
# (win-build-local.sh / mac-dev-build.sh) call it too, before building the
# component. See CLAUDE.md > Development > Unit tests.
test:
	XWIN_SDK="$(XWIN_SDK)" ./scripts/run-unit-tests.sh win

test-clean:
	rm -rf $(BUILD_WIN)/tests
	$(MAKE) test

mac-test:
	./scripts/run-unit-tests.sh mac

mac-test-clean:
	rm -rf $(BUILD_MAC)/tests
	$(MAKE) mac-test

# --- Windows component, local cross-compile (Linux host) ---
win-build:
	./scripts/win-build-local.sh

win-build-patch:
	./scripts/win-build-local.sh --patch

win-build-minor:
	./scripts/win-build-local.sh --minor

win-build-major:
	./scripts/win-build-local.sh --major

win-build-launch:
	./scripts/win-build-local.sh --launch

win-install:
	./scripts/install-windows.sh

win-logs:
	./scripts/navidrome-logs.sh $(ARGS)

win-test:
	./scripts/win-test.sh $(ARGS)

# --- macOS native component ---
mac-build:
	./scripts/mac-dev-build.sh

mac-build-patch:
	./scripts/mac-dev-build.sh --patch

mac-build-minor:
	./scripts/mac-dev-build.sh --minor

mac-build-major:
	./scripts/mac-dev-build.sh --major

mac-build-no-install:
	./scripts/mac-dev-build.sh --no-install

mac-install:
	./scripts/install-macos.sh

mac-release:
	./scripts/mac-dev-build.sh --new-release

mac-ci-build:
	@if [ -z "$(VERSION)" ]; then echo "usage: make mac-ci-build VERSION=x.y.z"; exit 1; fi
	./scripts/mac-ci-build.sh $(VERSION)

mac-logs:
	./scripts/navidrome-logs.sh $(ARGS)

# --- Windows-on-macOS VM testing (scripts/win-vm/) ---
win-vm-setup:
	./scripts/win-vm/setup-mac-toolchain.sh

win-vm-fetch:
	./scripts/win-vm/fetch-win11-arm.sh

win-vm-install:
	./scripts/win-vm/win-vm.sh install

win-vm-test:
	./scripts/win-vm/win-vm-test.sh $(ARGS)

# --- macOS-on-Linux VM testing (scripts/mac-vm/, Docker-OSX) ---
# One command. Orchestrates everything in scripts/mac-vm/mac-vm-up.sh; the only
# manual part is the first macOS installer (Apple has no unattended path).
mac-vm:
	./scripts/mac-vm/mac-vm-up.sh $(ARGS)

# Nuke everything (container + writable-layer disk + :snap image) and rebuild
# from zero, straight to the VNC installer. The macOS install clicks are still
# manual (nothing can automate those); everything around them is one command.
mac-vm-reinstall:
	./scripts/mac-vm/mac-vm.sh wipe
	MAC_VM_FORCE_INSTALL=1 ./scripts/mac-vm/mac-vm-up.sh $(ARGS)

mac-vm-setup:
	./scripts/mac-vm/setup-host.sh

mac-vm-install:
	./scripts/mac-vm/mac-vm.sh install

mac-vm-install-vnc:
	MAC_VM_VNC=1 ./scripts/mac-vm/mac-vm.sh install

mac-vm-run:
	./scripts/mac-vm/mac-vm.sh run

mac-vm-run-vnc:
	MAC_VM_VNC=1 ./scripts/mac-vm/mac-vm.sh run

# X11 guest -> VNC guest, keeping the installed macOS disk. The final 'install'
# step is interactive (foreground QEMU); run this from a real terminal.
# 'snapshot' verifies the :snap image exists (non-zero exit otherwise), so the
# 'rm' below is only reached once the disk is safely committed.
mac-vm-migrate-vnc:
	./scripts/mac-vm/mac-vm.sh stop
	./scripts/mac-vm/mac-vm.sh snapshot
	docker image inspect foo_navidrome-macvm:snap >/dev/null 2>&1 || { echo "no verified :snap — aborting before rm"; exit 1; }
	./scripts/mac-vm/mac-vm.sh rm
	IMAGE=foo_navidrome-macvm:snap MAC_VM_VNC=1 ./scripts/mac-vm/mac-vm.sh install

# Same, but reuse an existing foo_navidrome-macvm:snap (skip the slow re-commit).
# Guarded: refuses to 'rm' the running container if the snapshot isn't present.
mac-vm-reinstall-vnc:
	docker image inspect foo_navidrome-macvm:snap >/dev/null 2>&1 || { echo "no foo_navidrome-macvm:snap image — run 'make mac-vm-snapshot' first and confirm it printed a size"; exit 1; }
	./scripts/mac-vm/mac-vm.sh rm
	IMAGE=foo_navidrome-macvm:snap MAC_VM_VNC=1 ./scripts/mac-vm/mac-vm.sh install

mac-vm-vnc:
	./scripts/mac-vm/mac-vm.sh vnc

mac-vm-snapshot:
	./scripts/mac-vm/mac-vm.sh snapshot

mac-vm-snapshot-export:
	./scripts/mac-vm/mac-vm.sh snapshot-export $(FILE)

mac-vm-snapshot-import:
	./scripts/mac-vm/mac-vm.sh snapshot-import $(FILE)

mac-vm-stop:
	./scripts/mac-vm/mac-vm.sh stop

mac-vm-rm:
	./scripts/mac-vm/mac-vm.sh rm

mac-vm-logs:
	./scripts/mac-vm/mac-vm.sh logs

mac-vm-test:
	./scripts/mac-vm/mac-vm-test.sh $(ARGS)

mac-vm-ssh:
	./scripts/mac-vm/mac-vm.sh ssh

clean:
	rm -rf $(BUILD_WIN) $(BUILD_MAC)
