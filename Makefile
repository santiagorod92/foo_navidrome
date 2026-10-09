.PHONY: help test test-clean mac-test mac-test-clean \
	win-build win-build-release-log win-build-patch win-build-minor win-build-major win-build-launch win-install win-test win-logs win-ui-smoke win-ui \
	mac-build mac-build-patch mac-build-minor mac-build-major mac-build-no-install mac-install mac-release mac-ci-build mac-logs \
	win-vm-setup win-vm-fetch win-vm-install win-vm-test \
	mac-vm mac-vm-vnc mac-vm-open mac-vm-smoke mac-vm-ui mac-vm-test mac-vm-release mac-vm-build mac-vm-build-test \
	win11 win11-vnc win11-open win11-test win11-release win11-seed win11-smoke win11-ui win11-logs \
	audiomuse-up audiomuse-analyze audiomuse-status audiomuse-search audiomuse-logs audiomuse-down clean

XWIN_SDK ?= $(HOME)/.local/share/xwin/sdk
BUILD_WIN := build-win
BUILD_MAC := build-mac

help:
	@echo "foo_navidrome — make targets"
	@echo ""
	@echo "  test                  fast clang-cl+wine build/run of the unit tests (Linux)"
	@echo "  test-clean            same, forcing a clean recompile"
	@echo "  mac-test              native clang++ build/run of the SAME test suite (macOS)"
	@echo "  mac-test-clean        same, forcing a clean recompile"
	@echo ""
	@echo "  win-build             cross-compile Windows x64 component locally (win-build-local.sh, no version bump)"
	@echo "  win-build-patch       stamp last release + patch, then cross-compile"
	@echo "  win-build-minor       stamp last release + minor, then cross-compile"
	@echo "  win-build-major       stamp last release + major, then cross-compile"
	@echo "  win-build-launch      same as win-build, then relaunch local Wine foobar2000 to load it"
	@echo "  win-build-release-log build+relaunch with release logging (log in the profile, WARN+; verbose via Advanced prefs)"
	@echo "  win-install           install built DLL into local Wine foobar2000 + package"
	@echo "  win-logs              follow the local Wine debug log, colourised (run beside win-build-launch)"
	@echo "  win-ui-smoke          UI smoke test in the local Wine foobar2000: open browser, expand, play, assert log (needs win-build-launch'd DLL)"
	@echo "  win-ui                drive the Wine browser: ARGS='key 0x28' / 'click X Y' / 'shot' / 'wait REGEX' / 'prefs main' (scripts/ui-test.sh)"
	@echo "  win-test              dispatch build-windows.yml on GH runner, install, [ARGS=--launch]"
	@echo "                        (win-logs / mac-logs share scripts/navidrome-logs.sh — pass ARGS=-a for the whole file)"
	@echo ""
	@echo "  mac-build             xcodebuild Release, install locally (version = git describe)"
	@echo "  mac-build-patch       stamp last release + patch, build + install"
	@echo "  mac-build-minor       stamp last release + minor, build + install"
	@echo "  mac-build-major       stamp last release + major, build + install"
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
	@echo "  win11                 boot the Windows 11 VM (../windows-devbox), open its screen (VNC) in the browser, deploy the latest release [VNC=0: no browser]"
	@echo "  win11-vnc             boot the Windows 11 VM and open its screen (noVNC) in the browser — nothing deployed"
	@echo "  win11-open            open the VM screen (noVNC) in the browser once the container's viewer answers"
	@echo "  win11-test            cross-build the x64 DLL (debug log), deploy into the VM, relaunch"
	@echo "  win11-release         deploy a GitHub release [TAG=v1.12.0, default latest], relaunch"
	@echo "  win11-seed            copy foo_navidrome settings (server/account) from the Wine profile into the VM"
	@echo "  win11-smoke           build, deploy, run the UI smoke test on real Windows (scripts/win11/win11-ui-test.sh)"
	@echo "  win11-ui              win11-ui-test.sh ARGS='prefs radio' / 'browser' / 'log 50' — no build"
	@echo "  win11-logs            follow the guest's debug log (lands in the VM's shared folder)"
	@echo "  win11-<cmd>           any wvm command: win11-up, -down, -ssh, -shot, -dpi ARGS=144, -theme ARGS=dark, ..."
	@echo "                        (see ../macos-devbox/README.md; one-time: mvm setup, up, provision, snapshot base)"
	@echo ""
	@echo "  audiomuse-up          start the local AudioMuse-AI test stack (dev/audiomuse/, needs dev/audiomuse/.env)"
	@echo "  audiomuse-analyze     analyse the newest albums of the Navidrome in .env [ARGS=N albums]"
	@echo "  audiomuse-status      health, tasks and a sample text search; audiomuse-search ARGS='calm piano'"
	@echo "  audiomuse-logs        follow AudioMuse flask + worker logs"
	@echo "  audiomuse-down        stop it [ARGS=-v also deletes analysis + Ollama models]"
	@echo ""
	@echo "  clean                 remove local build-win/ artifacts"

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

win-build-release-log:
	./scripts/win-build-local.sh --release-log --launch

win-install:
	./scripts/install-windows.sh

win-logs:
	./scripts/navidrome-logs.sh $(ARGS)

win-ui-smoke:
	./scripts/ui-test.sh smoke

win-ui:
	./scripts/ui-test.sh $(ARGS)

win-test:
	./scripts/win-test.sh $(ARGS)

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
	./scripts/mac-dev-build.sh --patch --new-release

mac-ci-build:
	@if [ -z "$(VERSION)" ]; then echo "usage: make mac-ci-build VERSION=x.y.z"; exit 1; fi
	./scripts/mac-ci-build.sh $(VERSION)

mac-logs:
	./scripts/navidrome-logs.sh $(ARGS)

win-vm-setup:
	./scripts/win-vm/setup-mac-toolchain.sh

win-vm-fetch:
	./scripts/win-vm/fetch-win11-arm.sh

win-vm-install:
	./scripts/win-vm/win-vm.sh install

win-vm-test:
	./scripts/win-vm/win-vm-test.sh $(ARGS)

MVM ?= $(abspath ../macos-devbox/mvm)
COMPONENT ?= $(firstword $(shell ls -t foo_navidrome*.fb2k-component 2>/dev/null))

VNC ?= 1
MVM_ENV = $(dir $(MVM))mvm.env

mac-vm:
	$(MVM) up
	@if [ "$(VNC)" != 0 ]; then $(MAKE) --no-print-directory mac-vm-open; fi
	$(MVM) up --wait
	$(MAKE) mac-vm-release

mac-vm-vnc:
	$(MVM) up
	$(MAKE) --no-print-directory mac-vm-open
	$(MVM) up --wait

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

WVM ?= $(abspath ../windows-devbox/wvm)
WVM_ENV = $(dir $(WVM))wvm.env

win11:
	$(WVM) up
	@if [ "$(VNC)" != 0 ]; then $(MAKE) --no-print-directory win11-open; fi
	$(WVM) up --wait
	$(MAKE) win11-release

win11-vnc:
	$(WVM) up
	$(MAKE) --no-print-directory win11-open
	$(WVM) up --wait

win11-open:
	@eval "$$( [ -f "$(WVM_ENV)" ] && grep -E '^WVM_WEB_PORT=' "$(WVM_ENV)" )"; \
	  web=http://127.0.0.1:$${WVM_WEB_PORT:-8007}; \
	  echo "waiting for the VM screen at $$web ..."; \
	  for i in $$(seq 1 60); do curl -fs -o /dev/null "$$web" && break; sleep 2; done; \
	  curl -fs -o /dev/null "$$web" || { echo "VM screen not answering at $$web ($(WVM) logs)"; exit 1; }; \
	  $(WVM) web

win11-test:
	./scripts/win-build-local.sh --no-test
	$(WVM) deploy build-win/foo_navidrome.dll --launch

win11-release:
	$(WVM) deploy --gh santiagorod92/foo_navidrome$(if $(TAG),@$(TAG)) --launch

win11-seed:
	./scripts/win11/win11-ui-test.sh seed

win11-smoke:
	./scripts/win-build-local.sh --no-test
	./scripts/win11/win11-ui-test.sh smoke

win11-ui:
	./scripts/win11/win11-ui-test.sh $(ARGS)

win11-logs:
	tail -f "$$($(WVM) shared)/tmp/foo_navidrome_debug.log"

win11-%:
	$(WVM) $* $(ARGS)

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
