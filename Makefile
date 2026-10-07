.PHONY: help test test-clean mac-test mac-test-clean \
	win-build win-build-patch win-build-minor win-build-major win-build-launch win-install win-test win-logs win-ui-smoke win-ui \
	mac-build mac-build-patch mac-build-minor mac-build-major mac-build-no-install mac-install mac-release mac-ci-build mac-logs \
	win-vm-setup win-vm-fetch win-vm-install win-vm-test \
	mac-vm mac-vm-vnc mac-vm-open mac-vm-smoke mac-vm-ui mac-vm-test mac-vm-release mac-vm-build mac-vm-build-test \
	audiomuse-up audiomuse-analyze audiomuse-status audiomuse-search audiomuse-logs audiomuse-down clean

XWIN_SDK ?= $(HOME)/.local/share/xwin/sdk
BUILD_WIN := build-win
BUILD_MAC := build-mac

# Target naming: <os>-<action>. The OS prefix (win- / mac-) is the only thing
# that varies; the action after it means the same on both platforms, wired to
# whichever script performs those steps for that OS.
#   win-*  -> Windows component, cross-compiled on Linux (clang-cl + wine)
#   mac-*  -> native macOS component (xcodebuild)
#   win-vm-*  -> Windows component built + runtime-tested in a VM on macOS
#   mac-vm-*  -> macOS component runtime-tested in a local macOS VM on Linux (../macos-devbox)

help:
	@echo "foo_navidrome — make targets"
	@echo ""
	@echo "  test                  fast clang-cl+wine build/run of the unit tests (Linux)"
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
	@echo "  win-ui-smoke          UI smoke test in the local Wine foobar2000: open browser, expand, play, assert log (needs win-build-launch'd DLL)"
	@echo "  win-ui                drive the Wine browser: ARGS='key 0x28' / 'click X Y' / 'shot' / 'wait REGEX' (scripts/ui-test.sh)"
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
	@echo "  mac-vm                boot the macOS VM (../macos-devbox), open its screen (VNC) in the browser, deploy the latest release, launch [VNC=0: no browser]"
	@echo "  mac-vm-vnc            boot the macOS VM and open its screen (noVNC) in the browser — nothing deployed"
	@echo "  mac-vm-open           open the VM screen (noVNC) in the browser once macOS has booted"
	@echo "  mac-vm-smoke          build in the guest with the debug log, deploy, run the UI smoke test (scripts/mac-vm/mac-ui-test.sh)"
	@echo "  mac-vm-ui             mac-ui-test.sh ARGS='smoke [COMPONENT]' / 'browser' / 'log 50' — no build"
	@echo "  mac-vm-test           deploy COMPONENT=x.fb2k-component (default: newest in repo root), relaunch"
	@echo "  mac-vm-release        deploy a GitHub release [TAG=v1.12.0, default latest], relaunch"
	@echo "  mac-vm-build          build the macOS component INSIDE the guest (xcodebuild), pull the .fb2k-component here [ARGS=--clean]"
	@echo "  mac-vm-build-test     mac-vm-build, then deploy + launch it in the guest"
	@echo "  mac-vm-<cmd>          any mvm command: mac-vm-up, -down, -ssh, -shot, -snapshot, -restore ARGS=name, ..."
	@echo "                        (see ../macos-devbox/README.md; one-time: mvm setup, up, provision, snapshot base)"
	@echo ""
	@echo "  audiomuse-up          start the local AudioMuse-AI test stack (dev/audiomuse/, needs dev/audiomuse/.env)"
	@echo "  audiomuse-analyze     analyse the newest albums of the Navidrome in .env [ARGS=N albums]"
	@echo "  audiomuse-status      health, tasks and a sample text search; audiomuse-search ARGS='calm piano'"
	@echo "  audiomuse-logs        follow AudioMuse flask + worker logs"
	@echo "  audiomuse-down        stop it [ARGS=-v also deletes analysis + Ollama models]"
	@echo ""
	@echo "  clean                 remove local build-win/ artifacts"

# --- Unit tests (tests/*.cpp + src/core/*.cpp) ---
# One test suite, per-host toolchain. scripts/run-unit-tests.sh is the single
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

# UI smoke tests: scripts/ui-test.sh (Wine, input posted by tools/wclick.c) and
# scripts/mac-vm/mac-ui-test.sh (macOS VM, mvm VNC input) — same scenario, log assertions.
win-ui-smoke:
	./scripts/ui-test.sh smoke

win-ui:
	./scripts/ui-test.sh $(ARGS)

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

# --- macOS-on-Linux VM testing (../macos-devbox, `mvm`) ---
# The VM itself (install, snapshots, deploy, screenshots) is the sibling macos-devbox repo;
# only the foo_navidrome-specific steps live here. Any other mvm command passes through:
# `make mac-vm-ssh`, `make mac-vm-shot`, `make mac-vm-snapshot ARGS=before-x`, ...
MVM ?= $(abspath ../macos-devbox/mvm)
COMPONENT ?= $(firstword $(shell ls -t foo_navidrome*.fb2k-component 2>/dev/null))

# mac-vm opens the guest's screen (the container's noVNC page) in the browser once the guest has
# booted, then waits for its session and deploys. VNC=0 skips the browser tab. Same flow as
# foo_ui_panels' Makefile (both drive the one ../macos-devbox VM).
VNC ?= 1
MVM_ENV = $(dir $(MVM))mvm.env

mac-vm:
	$(MVM) up
	@if [ "$(VNC)" != 0 ]; then $(MAKE) --no-print-directory mac-vm-open; fi
	$(MVM) up --wait
	$(MAKE) mac-vm-release

# Just the VM + its screen: boot (no-op if already up), open noVNC, wait for the desktop session.
mac-vm-vnc:
	$(MVM) up
	$(MAKE) --no-print-directory mac-vm-open
	$(MVM) up --wait

# The guest's screen in the browser. Not before the guest is past OpenCore's boot picker: the
# picker boots the default disk after a short timeout, but any input cancels that timeout and a
# freshly connected noVNC tab sends pointer events — the VM would then sit at the picker. So wait
# for the guest's sshd (macOS is up), then open. Ports: mvm.env / environment, else the defaults.
mac-vm-open:
	@eval "$$( [ -f "$(MVM_ENV)" ] && grep -E '^MVM_(WEB|SSH)_PORT=' "$(MVM_ENV)" )"; \
	  web=http://127.0.0.1:$${MVM_WEB_PORT:-8006}; ssh=$${MVM_SSH_PORT:-50922}; \
	  echo "waiting for macOS to boot (sshd on :$$ssh) before opening $$web ..."; \
	  for i in $$(seq 1 180); do \
	    timeout 6 bash -c "exec 3<>/dev/tcp/127.0.0.1/$$ssh && head -c4 <&3" 2>/dev/null | grep -q '^SSH-' && break; \
	    sleep 5; \
	  done; \
	  curl -fs -o /dev/null "$$web" || { echo "VM screen not answering at $$web (make mac-vm-logs)"; exit 1; }; \
	  $(MVM) web

mac-vm-test:
	@test -n "$(COMPONENT)" || { echo "no foo_navidrome*.fb2k-component here — pass COMPONENT=path, or: make mac-vm-release"; exit 1; }
	$(MVM) deploy "$(COMPONENT)" --launch

mac-vm-release:
	$(MVM) deploy --gh santiagorod92/foo_navidrome$(if $(TAG),@$(TAG)) --launch

# Build the macOS component in the guest (xcodebuild) and pull the packaged
# .fb2k-component back to the repo root. No version bump. Needs Xcode in the
# guest once (`mvm xcode Xcode_15.x.xip`, then `mvm snapshot xcode`).
# ARGS=--clean wipes the guest build tree first.
mac-vm-smoke:
	./scripts/mac-vm/mac-vm-build.sh --debug-log --no-unit-tests
	./scripts/mac-vm/mac-ui-test.sh smoke

mac-vm-ui:
	./scripts/mac-vm/mac-ui-test.sh $(ARGS)

mac-vm-build:
	./scripts/mac-vm/mac-vm-build.sh $(ARGS)

mac-vm-build-test:
	./scripts/mac-vm/mac-vm-build.sh --test $(ARGS)

mac-vm-%:
	$(MVM) $* $(ARGS)

# --- Local AudioMuse-AI test stack (dev/audiomuse/, scripts/audiomuse-dev.sh) ---
audiomuse-up:
	./scripts/audiomuse-dev.sh up

audiomuse-analyze:
	./scripts/audiomuse-dev.sh analyze $(ARGS)

audiomuse-status:
	./scripts/audiomuse-dev.sh status

audiomuse-search:
	./scripts/audiomuse-dev.sh search $(ARGS)

audiomuse-logs:
	./scripts/audiomuse-dev.sh logs

audiomuse-down:
	./scripts/audiomuse-dev.sh down $(ARGS)

clean:
	rm -rf $(BUILD_WIN) $(BUILD_MAC)
