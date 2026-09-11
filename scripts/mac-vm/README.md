# Local macOS testing on Linux (no Mac) — Docker-OSX

Runtime-test the macOS `foo_navidrome.component` in a QEMU/KVM macOS VM
([sickcodes/docker-osx](https://github.com/sickcodes/Docker-OSX)) — all from the
terminal, no CI round-trip, no Apple hardware. The macOS mirror of
[`scripts/win-vm/`](../win-vm/).

> **Apple's macOS EULA permits virtualization only on Apple-branded hardware.**
> This harness is a local development-testing aid; know your obligations before
> using it. It is not wired into CI and ships no macOS image.

## What it does / doesn't do

- **Does**: boot x86_64 macOS in Docker, drop a prebuilt `.fb2k-component` into
  the guest over SSH, ad-hoc re-sign it, relaunch foobar2000. Same deploy loop as
  `win-vm-test.sh`.
- **Can also build, on demand** (`mac-vm-build.sh`), once Xcode is installed in
  the guest with `mac-vm.sh provision-xcode`. That is a one-time manual
  `Xcode_15.x.xip` drop (Apple ID required to download it) — bake it into the
  snapshot afterwards and it is never redone. See **Building in the guest**
  below. The default flow still doesn't need Xcode: the component comes from the
  `macos-14` CI runner (`gh release download`, or a build artifact) or a real Mac
  (`scripts/mac-ci-build.sh`).
- **x86_64 slice required.** The guest is an emulated Intel Mac. A CI build is a
  universal binary and satisfies this; a guest build here is x86_64-only, which
  the guest also runs; a local arm64-only Mac build will not load.
  `mac-vm-test.sh` runs `lipo -archs` and warns.
- **Not "test anywhere".** Needs a Linux host with `/dev/kvm` + nested virt. Does
  not run on Docker Desktop (macOS/Windows) or GitHub Actions.

## Requirements

- Linux, `/dev/kvm` present and writable by your user (`kvm` group)
- Docker daemon reachable by your user (`docker` group)
- `sshpass`, `unzip`, and `gh` (for `--release`)
- ~40 GB free disk, a few GB image pull

## Just run it

```bash
make mac-vm-setup     # once: host packages, kvm/docker checks, image pull, gh auth (gh auth login)
make mac-vm           # everything else, every time
```

`make mac-vm` (→ `mac-vm-up.sh`) does the whole chain itself: create or boot the
guest, wait for it, **snapshot the install** so it's never lost, install
`foobar2000.app`, deploy the newest component (`--release --launch` by default,
override with `ARGS=`), launch it.

The **one** thing it can't automate is the *first* macOS install — Apple ships no
unattended installer and Docker-OSX's image has no `:auto` variant. On the first
`make mac-vm` it boots the installer over VNC, opens `remmina`, prints the exact
clicks (Disk Utility → Reinstall → Setup Assistant → **Remote Login: on**), and
waits. The moment SSH answers it takes over and snapshots to
`foo_navidrome-macvm:snap`. Every `make mac-vm` after that is hands-off, ~1 min.

Resume a first install that was interrupted: just run `make mac-vm` again.

**Guest broken? `make mac-vm-reinstall`** — one command: wipes the container, its
writable-layer disk and the `:snap` image, then rebuilds from zero straight to
the VNC installer (ignoring any snapshot / `.tar.zst`). Only the macOS install
clicks stay manual; all the docker/QEMU setup is automatic.

### Do the install once, ever

There is **no** automated macOS install (checked: Apple has none; Docker-OSX's
prebuilt-image URL `images.sick.codes/mac_hdd_ng_auto.img` is dead; no expect
scripts). So after the first install, back it up:

```bash
make mac-vm-snapshot-export                 # -> foo_navidrome-macvm-snap.tar.zst
```

Keep that file anywhere. On a wiped Docker, a new machine, or after
`make mac-vm-rm`: drop it in the repo root (or `make mac-vm-snapshot-import
FILE=/path/to/it`) and `make mac-vm` boots straight from it — no reinstall.
`mac-vm-up.sh` auto-imports `foo_navidrome-macvm-snap.tar.zst` if it sees it in
`$PWD`.

## Manual / step-by-step setup

```bash
scripts/mac-vm/setup-host.sh          # host packages + kvm/docker checks + image pull
scripts/mac-vm/mac-vm.sh install      # GUI macOS install — manual, ~30-45 min
```

> **Run `install` directly in a terminal, not `make mac-vm-install`, and leave
> that terminal open until macOS is fully installed.** The container is
> `docker run -it` with QEMU on `-monitor stdio`; if stdin isn't a live TTY
> (piped through `make`, terminal closed) QEMU takes EOF and exits `0` a few
> minutes in — which looks exactly like a boot hang. The script prints a warning
> when it can't see a TTY.

`mac-vm.sh install` opens the macOS installer (in an X11 window, or over VNC if
`MAC_VM_VNC=1`). Inside the guest:

1. **Disk Utility** → erase the largest *Apple Inc. VirtIO* disk as APFS → quit
2. **Reinstall macOS** → pick that disk → wait (it reboots itself a few times)
3. At the **OpenCore picker** after each reboot, choose **macOS** (the plain
   internal disk) — *not* *macOS Installer* / *Recovery*. The picker does not
   default to the right entry; letting it time out on the wrong one reads as a
   hang. First real boot then rebuilds kext caches (10–20 min, progress bar
   barely moves — don't kill it).
4. Finish Setup Assistant (skip Apple ID)
5. **System Settings → General → Sharing → Remote Login: on**
   (or Terminal: `sudo systemsetup -setremotelogin on`)
6. Download the **Intel** foobar2000 from foobar2000.org and drag
   `foobar2000.app` into `/Applications`

The macOS disk lives in the container's writable layer. `mac-vm.sh snapshot`
`docker commit`s it so you can roll back; `mac-vm.sh rm` throws it away.

## Building in the guest (no Mac)

One-time, install Xcode into the guest:

```bash
# 1. Download Xcode_15.4.xip from https://developer.apple.com/download/all/
#    (Apple ID required; 15.4 matches the macos-14 CI toolchain) -> repo root
scripts/mac-vm/mac-vm.sh run                 # guest must be booted, Remote Login on
make mac-vm-provision-xcode                  # scp the .xip in, expand, xcode-select
                                            #   (slow under emulation: ~20-40 min)
make mac-vm-snapshot && make mac-vm-snapshot-export   # so it is never redone
```

Then, every build:

```bash
make mac-vm-build          # push SDK siblings + working tree -> guest,
                           #   run unit tests, xcodebuild Release, package,
                           #   pull foo_navidrome_<v>.fb2k-component to repo root
make mac-vm-build-test     # ... then deploy + launch it in the same guest
make mac-vm-build ARGS=--clean   # wipe the guest ~/build tree first
```

Notes:

- **No version bump.** `mac-vm-build.sh` runs `mac-ci-build.sh "$(cat
  version.txt)"`, so `version.txt` is rewritten to its current value.
- **DerivedData is kept** in the guest at
  `~/build/foobar2000/foo_navidrome/build/` between runs; `--clean` drops it.
- **SDK siblings** (`../foobar2000/{SDK,helpers,shared,foobar2000_component_client,helpers-mac}`,
  `../pfc`, `../libPPUI`) are pushed from the host each run — the same ~5 MB tree
  a local Mac build uses.
- **Slow.** xcodebuild under the KVM-accelerated-but-emulated x86 guest is
  ~15-40 min vs the CI runner's ~3 min. Fine on demand, not a tight loop — for
  that, use `make win-build` (Windows) or a real Mac.
- **Disk.** Xcode expanded is ~40 GB in the guest; the snapshot / `.tar.zst`
  grows accordingly (plan for ~70-90 GB). Make sure the host has the room before
  `provision-xcode` and `snapshot`.
- **EULA.** Apple's macOS EULA permits virtualization only on Apple hardware;
  this is a local dev-testing aid, not shipped or wired into CI (unchanged from
  the run/test-only harness).

## The test loop

```bash
scripts/mac-vm/mac-vm.sh run                          # boot the installed guest (detached)
scripts/mac-vm/mac-vm-test.sh --release --launch      # pull latest CI build -> deploy -> relaunch
```

Other component sources:

```bash
scripts/mac-vm/mac-vm-test.sh path/to/foo_navidrome_1.12.0.fb2k-component --launch
scripts/mac-vm/mac-vm-test.sh --release=v1.12.0
scripts/mac-vm/mac-vm-test.sh                         # newest *.fb2k-component in the repo root
```

Then in the guest: **File ▸ Open Navidrome Browser**, or
**Preferences ▸ Media Library ▸ Navidrome**. `mac-vm.sh ssh` drops you into a
shell; `mac-vm.sh logs` follows the boot log; `mac-vm.sh stop` powers it off.

## Files

| file | role |
|------|------|
| `setup-host.sh`   | host packages (docker, sshpass), kvm/docker preflight, image pull |
| `mac-vm.sh`       | pull / install / run / ssh / **provision-xcode** / snapshot / rm the Docker-OSX container |
| `mac-vm-test.sh`  | resolve a `.fb2k-component` → deploy over SSH → re-sign → relaunch |
| `mac-vm-build.sh` | push SDK + working tree → guest, xcodebuild Release, package, pull the `.fb2k-component` back (needs `provision-xcode` once) |

## Notes / gotchas

- **One image, version chosen at run time.** Docker Hub ships only
  `sickcodes/docker-osx:latest`; the macOS release comes from `-e SHORTNAME=`
  (`MACOS_VERSION` env, default `sonoma` — also `ventura`, `monterey`, `big-sur`,
  `catalina`, `sequoia`). There is no `:sonoma` tag.
- **X11 mode: `run` re-runs `xhost +local:root`.** The grant is tied to your host
  login session and is lost on re-login. If `run` were a bare `docker start` (as
  it first was), the container couldn't open `DISPLAY`, and QEMU — which has no
  `-display` flag, so it needs the X server — exits `0` minutes into the boot.
  That looks like the VM freezing; it's actually X auth. `run` now re-asserts the
  grant every time.
- **Wayland + NVIDIA proprietary → use VNC, not X11.** X11 passthrough also needs
  `xhost` installed (Arch: `xorg-xhost`) and a host GL stack the containerised
  QEMU GTK can use. The NVIDIA proprietary driver isn't visible in the container
  (`glx: failed to create dri3 screen`, `failed to load driver: nvidia-drm`), so
  the window is black even though QEMU is running. Run everything with
  `MAC_VM_VNC=1` and connect a VNC client: `scripts/mac-vm/mac-vm.sh vnc` prints
  a `remmina` / `vncviewer` line (no password). To move a working X11 install
  onto VNC without reinstalling:

  ```bash
  make mac-vm-migrate-vnc          # stop + snapshot + rm + reinstall from the snapshot as VNC
  # equivalently, by hand:
  scripts/mac-vm/mac-vm.sh stop
  scripts/mac-vm/mac-vm.sh snapshot
  scripts/mac-vm/mac-vm.sh rm
  IMAGE=foo_navidrome-macvm:snap MAC_VM_VNC=1 scripts/mac-vm/mac-vm.sh install
  ```

  Fresh (no existing install to keep): `make mac-vm-install-vnc`, then
  `make mac-vm-run-vnc` for later boots. `make mac-vm-vnc` prints the client
  command.
- **Switching X11 ↔ VNC without reinstalling.** The GUI transport is a
  `docker create` arg, fixed at `install`. To move a working install across:
  `mac-vm.sh snapshot`, then `mac-vm.sh rm`, then
  `IMAGE=<container>:snap MAC_VM_VNC=1 mac-vm.sh install` — `IMAGE=` reuses the
  snapshot as the base and skips the multi-GB macOS download.
- **`NOPICKER`** (env, default `false`) is passed straight through to docker-osx.
  Leave it `false` until the OpenCore installer/recovery entries are gone;
  `NOPICKER=true` then lets an unattended `run` boot straight to macOS.
- **Default credentials** are docker-osx's throwaway `user` / `alpine`, SSH on
  host `localhost:50922`. Host-only; never reuse them.
- **`docker start` reboots the VM** from the same writable-layer disk — that's
  how `run` reuses the install.
- **Re-signing in the guest is mandatory.** macOS SIGKILLs a bundle whose
  signature no longer matches its bytes after the `tar` copy; the script runs
  `codesign --sign - --force --deep`, same as `install-macos.sh`.
- **x86_64 slice required** — see "What it does / doesn't do".
- First boot after `install` is slow (Setup Assistant + first login). Give SSH a
  few minutes to answer.
