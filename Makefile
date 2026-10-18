.PHONY: help test test-clean mac-test mac-test-clean i18n-check i18n-missing \
	win-build win-build-x86 win-build-release-log win-build-patch win-build-minor win-build-major win-build-launch win-install win-test win-logs win-ui-smoke win-ui ui-compare \
	mac-syntax mac-build mac-build-patch mac-build-minor mac-build-major mac-build-no-install mac-install mac-release mac-ci-build mac-logs \
	win-setup-toolchain-mac win-build-mac \
	mac-vm mac-vm-vnc mac-vm-open mac-vm-smoke mac-vm-ui mac-vm-test mac-vm-release mac-vm-build mac-vm-build-test \
	win11 win11-vnc win11-open win11-test win11-release win11-seed win11-smoke win11-ui win11-logs \
	win11-arm win11-arm-vnc win11-arm-web win11-arm-up win11-arm-open win11-arm-ssh win11-arm-logs win11-arm-down win11-arm-status win11-arm-prepare win11-arm-reset win11-arm-shot \
	win11-arm-provision win11-arm-deploy win11-arm-seed win11-arm-fb2k win11-arm-log win11-arm-test win11-arm-cfg \
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
	@echo "  mac-syntax            compile-check the shared SDK glue + macOS .mm sources against the SDK, no Xcode needed (ARGS=files)"
	@echo "  i18n-check            every tr()/NDTr()/trw() string has a Spanish entry (also run by both test targets)"
	@echo "  i18n-missing          print the UI strings that still lack a Spanish translation"
	@echo ""
	@echo "  win-build             cross-compile Windows x64 component locally (win-build-local.sh, no version bump)"
	@echo "  win-build-x86         cross-compile the 32-bit (x86) DLL -> build-win-x86/ + _win-x86.fb2k-component"
	@echo "  win-build-patch       stamp last release + patch, then cross-compile"
	@echo "  win-build-minor       stamp last release + minor, then cross-compile"
	@echo "  win-build-major       stamp last release + major, then cross-compile"
	@echo "  win-build-launch      same as win-build, then relaunch local Wine foobar2000 to load it"
	@echo "  win-build-release-log build+relaunch with release logging (log in the profile, WARN+; verbose via Advanced prefs)"
	@echo "  win-install           install built DLL into local Wine foobar2000 + package"
	@echo "  win-logs              follow the local Wine debug log, colourised (run beside win-build-launch)"
	@echo "  ui-compare            same UI scenario on several platforms + a step matrix: ARGS='playqueue' (default wine win11) / 'smoke wine win11 mac'"
	@echo "  win-ui-smoke          UI smoke test in the local Wine foobar2000: open browser, expand, play, assert log (needs win-build-launch'd DLL)"
	@echo "  win-ui                ui-test.sh wine ARGS='playqueue' / 'search' / 'i18n' / 'offline' / 'solo' / 'prefs main' / 'key down' / 'shot'"
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
	@echo "  win-setup-toolchain-mac  one-time on a Mac: clang-cl/lld/xwin/WTL + foobar SDK + colima/docker"
	@echo "  win-build-mac         cross-compile the Windows x64 DLL on a Mac -> build-win-mac/foo_navidrome.dll"
	@echo ""
	@echo "  mac-vm                boot the macOS VM (../macos-devbox), open its screen (VNC) in the browser, deploy the latest release, launch [VNC=0: no browser]"
	@echo "  mac-vm-vnc            boot the macOS VM and open its screen (noVNC) in the browser — nothing deployed"
	@echo "  mac-vm-open           open the VM screen (noVNC) in the browser once macOS has booted"
	@echo "  mac-vm-smoke          build in the guest with the debug log, deploy, run the UI smoke test (scripts/ui-test.sh mac smoke)"
	@echo "  mac-vm-ui             ui-test.sh mac ARGS='smoke [COMPONENT]' / 'playqueue' / 'i18n' / 'solo' / 'browser' / 'log 50' — no build"
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
	@echo "  win11-smoke           build, deploy, run the UI smoke test on real Windows (scripts/ui-test.sh win11 smoke)"
	@echo "  win11-ui              ui-test.sh win11 ARGS='playqueue' / 'search' / 'cui' / 'i18n' / 'offline' / 'solo' / 'prefs radio' — no build"
	@echo "  win11-arm             Apple Silicon Mac: start the Windows 11 ARM VM (QEMU + HVF) and open its screen [VNC=0: no browser]"
	@echo "  win11-arm-vnc         same as win11-arm (start + open the noVNC screen)"
	@echo "  win11-arm-web         open the VM screen (noVNC, http://127.0.0.1:8006) once it answers; win11-arm-open does the same"
	@echo "  win11-arm-up          start the VM only (QEMU + the noVNC page as launchd jobs; first boot installs Windows)"
	@echo "  win11-arm-down        stop the VM (the Windows disk is kept); win11-arm-reset also deletes it"
	@echo "  win11-arm-ssh         ssh into the guest (tester/tester, :2223) [ARGS=command]"
	@echo "  win11-arm-logs        follow the QEMU + serial log"
	@echo "  win11-arm-status      VM, screen and ssh state"
	@echo "  win11-arm-shot        save the VM screen to build/ui-test/ [ARGS=file.png]"
	@echo "  win11-arm-provision   one-time after the install: ssh key + foobar2000 (ARM) in the guest"
	@echo "  win11-arm-deploy      build the DLL (win-build-mac) and install it in the guest's foobar2000"
	@echo "  win11-arm-seed        copy this Mac's foo_navidrome settings (server, account) into the guest"
	@echo "  win11-arm-fb2k        foobar2000 in the guest: ARGS='start|stop|cmd <menu command>'"
	@echo "  win11-arm-log         end of the guest's foo_navidrome.log [ARGS='LINES REGEX']"
	@echo "  win11-arm-test        unit tests built for Windows, run in the guest"
	@echo "  win11-arm-cfg         run ARGS=\"SQL\" on the guest's config.sqlite (stops foobar2000)"
	@echo "  win11-arm-prepare     one-time: fetch the Windows 11 ARM ISO + unattended setup through dockur (run by -up when missing)"
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

mac-syntax:
	./scripts/mac-syntax-check.sh $(ARGS)

i18n-check:
	python3 scripts/i18n-check.py

i18n-missing:
	python3 scripts/i18n-check.py --missing

mac-test-clean:
	rm -rf $(BUILD_MAC)/tests
	$(MAKE) mac-test

win-build:
	./scripts/win-build-local.sh

win-build-x86:
	./scripts/win-build-local.sh --x86

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
	./scripts/ui-test.sh wine smoke

ui-compare:
	./scripts/ui-test.sh compare $(ARGS)

win-ui:
	./scripts/ui-test.sh wine $(ARGS)

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

win-setup-toolchain-mac:
	./scripts/win-setup-toolchain-mac.sh

win-build-mac:
	./scripts/win-build-mac.sh

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
	./scripts/ui-test.sh mac smoke

mac-vm-ui:
	./scripts/ui-test.sh mac $(ARGS)

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
	./scripts/ui-test.sh win11 seed

win11-smoke:
	./scripts/win-build-local.sh --no-test
	./scripts/ui-test.sh win11 smoke

win11-ui:
	./scripts/ui-test.sh win11 $(ARGS)

win11-logs:
	tail -f "$$($(WVM) shared)/tmp/foo_navidrome_debug.log"

win11-%:
	$(WVM) $* $(ARGS)

win11-arm:
	./scripts/win11-arm.sh up
	@if [ "$(VNC)" != 0 ]; then ./scripts/win11-arm.sh open; fi

win11-arm-vnc:
	./scripts/win11-arm.sh up
	./scripts/win11-arm.sh open

win11-arm-web:
	./scripts/win11-arm.sh open

win11-arm-up:
	./scripts/win11-arm.sh up

win11-arm-open:
	./scripts/win11-arm.sh open

win11-arm-ssh:
	./scripts/win11-arm.sh ssh $(ARGS)

win11-arm-logs:
	./scripts/win11-arm.sh logs

win11-arm-down:
	./scripts/win11-arm.sh down

win11-arm-status:
	./scripts/win11-arm.sh status

win11-arm-prepare:
	./scripts/win11-arm.sh prepare

win11-arm-reset:
	./scripts/win11-arm.sh reset

win11-arm-shot:
	./scripts/win11-arm.sh shot $(ARGS)

win11-arm-provision:
	./scripts/win11-arm.sh provision

win11-arm-deploy: win-build-mac
	./scripts/win11-arm.sh deploy

win11-arm-seed:
	./scripts/win11-arm.sh seed

win11-arm-fb2k:
	./scripts/win11-arm.sh fb2k $(ARGS)

win11-arm-log:
	./scripts/win11-arm.sh fb2k log $(ARGS)

win11-arm-test:
	./scripts/run-unit-tests.sh win11-arm

win11-arm-cfg:
	./scripts/win11-arm.sh cfg "$(ARGS)"

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
