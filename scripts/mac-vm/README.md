# macOS testing on Linux (no Mac)

The macOS VM is the sibling repo **`../macos-devbox`** and its `mvm` CLI. It runs
[dockur/macos](https://github.com/dockur/macos) on KVM, keeps the disk on a host bind
mount (Docker prune can't touch it), takes instant btrfs snapshots, shows the screen in
the browser, and handles screenshots/input from the host. Its README covers the one-time
setup (manual macOS install, `mvm provision`, `mvm snapshot base`).

This folder keeps only the foo_navidrome-specific part: **building in the guest**.

## Test loop

```bash
make mac-vm                          # boot the VM, deploy the latest GitHub release, launch
make mac-vm-test                     # deploy the newest ./foo_navidrome*.fb2k-component
make mac-vm-test COMPONENT=path/to/foo_navidrome_1.17.2.fb2k-component
make mac-vm-release TAG=v1.12.0      # a specific release
make mac-vm-shot                     # screenshot -> ../macos-devbox/shots/
make mac-vm-ssh                      # any other mvm command: mac-vm-<cmd> [ARGS=...]
```

Then in the guest: **File ▸ Open Navidrome Browser**, or
**Preferences ▸ Media Library ▸ Navidrome**.

The guest is an emulated **Intel** Mac, so the bundle needs an x86_64 slice. The
universal CI build has one. A local arm64-only `mac-dev-build.sh` build won't load, and
`mvm deploy` refuses it.

## Building in the guest (`mac-vm-build.sh`)

One time, install Xcode in the guest (15.4 matches the `macos-14` CI toolchain; an Apple
ID is required to download it):

```bash
../macos-devbox/mvm xcode ~/Downloads/Xcode_15.4.xip    # slow under emulation: ~20-40 min
../macos-devbox/mvm snapshot xcode
```

Then, for every build:

```bash
make mac-vm-build          # push SDK siblings + working tree -> guest, unit tests,
                           #   xcodebuild Release, package, pull the .fb2k-component back
make mac-vm-build-test     # ... then deploy + launch it
make mac-vm-build ARGS=--clean   # wipe the guest ~/build tree first
```

- **No version bump.** It runs `mac-ci-build.sh "$(scripts/version.sh)"`, resolved on the host (git describe, e.g. `1.21.1-dev.3+2a46400`; the guest copy has no `.git`).
- **DerivedData is kept** in the guest at `~/build/foobar2000/foo_navidrome/build/`.
- **It's slow.** Emulated xcodebuild takes ~15–40 min, compared with ~3 min on CI. It's
  fine on demand, but not for a tight loop.
