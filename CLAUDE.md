# foo_navidrome

## Overview

foobar2000 v2 component that browses and streams from Navidrome / Subsonic-compatible servers. Cross-platform: macOS (shipped) and Windows x86/x64/ARM64EC (shipped, runtime-verified on native Windows). One `.fb2k-component` carries all platforms. Two entry points on macOS: **File › Open Navidrome Browser** and **Preferences › Media Library › Library viewers › Navidrome**.

## Architecture

Shared C++ core + per-platform UI/HTTP layers:

- `main.cpp`, `stdafx.h`, `SubsonicTypes.h` — cross-platform shared code (component version, pure-C++ data types)
- `SubsonicClient.h/.mm` — macOS Subsonic HTTP client (ObjC/NSURLSession)
- `Windows/SubsonicClientWin.h/.cpp` — Windows Subsonic client (WinHTTP)
- `NavidromePlugin.mm` — macOS registration: `cfg_string` for URL/user/pass/salt, `preferences_page` under Tools, `mainmenu_commands` under File menu, `library_viewer` factory
- `NavidromeInput.h/.mm` — `input_singletrack` handler for the `navidrome://track/<id>?...` URI scheme. Parses metadata from the URI for `get_info()`; on `decode_initialize()`, resolves the current HTTP stream URL via SubsonicClient and opens a nested `input_decoder` via `input_entry::g_open_for_decoding(..., fromRedirect=true)`. Registered with `input_entry::flag_redirect`.
- `NavidromeArtExtractor.mm` — implements `album_art_extractor` so Now Playing / playlists show art fetched from Navidrome
- `Mac/NavidromeBrowserController.*` — `NSViewController` housing the Artists/Albums/Songs tree, search, action buttons. Mounted in two places: (a) directly inside the *Preferences › Media Library › Navidrome* prefs sub-page; (b) wrapped in a standalone `NSWindow` by `NavidromeShowStandaloneBrowser()` (called by the File menu and `library_viewer.activate()`). Each mount point creates a fresh controller — Cocoa requires each `NSView` to have a single superview, so the previous shared-singleton pattern would have broken dual-mount. Enqueues tracks as `navidrome://` URIs (not raw HTTP URLs).
- `Mac/NavidromePreferencesController.*` — `NSViewController` preferences UI
- `Windows/NavidromePluginWin.cpp`, `Windows/BrowserWindow.*` — Windows ATL equivalents. Ported to the `navidrome://` URI scheme (`BrowserWindow::enqueueNodes` builds `navidrome://track/` URIs; `NavidromeInputWin` decodes them). `BrowserWindow` can mount two ways like the Mac controller: a standalone window (`show()`, singleton — File menu / `library_viewer`) **and** an inline `WS_CHILD` panel (`createEmbedded()`) hosted in the *Preferences › Media Library › Navidrome* sub-page. Right-click on the tree opens a Play Now / Add to Playlist context menu (`WM_CONTEXTMENU`).

GUIDs for cfg vars / prefs page / menu commands are hardcoded constants in `NavidromePlugin.mm` (lines 11–17) — must be regenerated when forking.

Credentials persist via `cfg_string` (foobar2000 config store). Password is sent via Subsonic token auth (md5 of password + salt) by `SubsonicClient`.

## Development

### Build layout (siblings required, see README)

```
foobar2000/
  SDK/, helpers/, shared/, foobar2000_component_client/
  foo_navidrome/        ← this repo
pfc/                    ← sibling of foobar2000/
```

### macOS

1. Open `foo_navidrome.xcworkspace` in Xcode (must use the workspace, not the bare xcodeproj — pulls in SDK projects)
2. Build the `foo_navidrome` scheme
3. `./scripts/install-macos.sh` copies the built component into foobar2000's user-components dir
4. Restart foobar2000

### Windows

Visual Studio 2022. `Windows/foo_navidrome.vcxproj` — must update `<ProjectReference>` GUIDs to match the local SDK projects. Build Release|x64, copy `.dll` to `%APPDATA%\foobar2000\user-components\foo_navidrome\`.

### Windows testing on macOS (no PC / no Wine)

`scripts/win-vm/` cross-compiles the **x64** Windows DLL on macOS (`clang-cl` + `lld-link` + `xwin` + WTL) and runtime-tests it in a headless Windows 11 ARM64 QEMU/HVF guest. `setup-mac-toolchain.sh` (once) → `fetch-win11-arm.sh` (build ISO) → `win-vm.sh install` (unattended install) → `win-vm-test.sh --launch` (build → deploy over SSH → relaunch). See `scripts/win-vm/README.md`. clang can't cross-compile ARM64EC (MSVC-only intrinsics); the x64 build runs under emulation on ARM foobar, which is sufficient for UI testing — CI still builds the real ARM64EC binary.

### Versioning + dev loop

Single source of truth for the version is `version.txt` (tracked, e.g. `1.0.3`). The Xcode "Generate Version Header" build phase reads it on every build (declared `alwaysOutOfDate = 1` + listed in `inputPaths`) and writes `version_generated.h` (gitignored) used by `main.cpp` and `install-macos.sh`.

```bash
./scripts/dev-build.sh                # bump patch, xcodebuild Release, install-macos.sh
./scripts/dev-build.sh --minor        # bump minor (resets patch to 0)
./scripts/dev-build.sh --major        # bump major (resets minor + patch)
./scripts/dev-build.sh --no-bump      # rebuild + install with current version (no bump)
./scripts/dev-build.sh --no-install   # bump + build only (skip install-macos.sh)
./scripts/dev-build.sh --new-release  # bump, build, install, then gh release create
```

`install-macos.sh` still works standalone (assumes a build already exists in DerivedData) — `dev-build.sh` is the wrapper that builds first.

### Release pipeline

Automated via `.github/workflows/release.yml` on every push to `main`. Uses [semantic-release](https://semantic-release.gitbook.io/) (config in `.releaserc.json`) reading [Conventional Commits](https://www.conventionalcommits.org/):

- `feat:` → minor bump · `fix:`/`perf:`/`refactor:` → patch bump · `chore:`/`docs:`/`style:`/`test:`/`ci:` → no release · breaking change footer or `!` → major bump.
- Plugin chain: `commit-analyzer` → `release-notes-generator` → `changelog` (writes `CHANGELOG.md`) → `exec` (calls `ci-build.sh <version>` which runs xcodebuild + packages `foo_navidrome_<version>.fb2k-component`) → `git` (commits `version.txt` + `CHANGELOG.md` with `[skip ci]`) → `github` (creates release with the `.fb2k-component` attached).
- SDK source: the workflow clones `marc2k3/foobar2000-sdk` and `marc2k3/pfc` into the sibling-directory layout the project expects (`pfc/`, `foobar2000/{SDK,helpers,shared,foobar2000_component_client,helpers-mac,foo_navidrome}`).
- First-time setup: tag the current state (`git tag v$(cat version.txt) && git push --tags`) so semantic-release has a starting reference; otherwise it treats the next release as `v1.0.0`.

### Manual release (fallback, bypasses CI)

```bash
./scripts/dev-build.sh --new-release    # bump, build, install, package, gh release create
```

## Decisions & Constraints

- **Media Library integration: native via custom URI + library_viewer.** foobar2000's *Preferences › Media Library › Music Folders* only accepts filesystem paths — there is no public SDK hook to register a remote source. The native-feeling integration uses two SDK extensions:
  1. `input_singletrack` for the `navidrome://track/<id>?title=...&artist=...&album=...&tracknumber=N&date=YYYY&duration=SEC&coverArt=...&suffix=mp3` URI scheme. Metadata is embedded in the URI so playlists render without a network round-trip; the actual HTTP stream URL is resolved at decode time from current credentials, so playlists survive credential rotation / server URL changes. Implemented in `NavidromeInput.mm`.
  2. `library_viewer` registration in `NavidromePlugin.mm` — the browser appears in *Preferences › Media Library › Library viewers* and can be activated from there.

- **Embedded browser via the Media Library prefs sub-page.** Instead of going through `ui_element_mac` (which would surface the browser as a draggable DUI panel in the main layout), the browser is mounted directly as the content of the *Preferences › Media Library › Navidrome* sub-page — the prefs page's `instantiate()` returns the `NavidromeBrowserController` (an `NSViewController`). This gives users a native inline browser at zero refactor cost: the same view controller code is used by the standalone window. A real `ui_element_mac` layout panel is still a possible future iteration if users want to dock the browser inside the main layout, but the prefs-page mount covers the day-to-day "see my library without opening a separate window" use case.

- **Input handler is registered as a redirect** (`input_entry::flag_redirect`) and opens nested decoders via `g_open_for_decoding(..., fromRedirect=true)` to prevent recursion. This keeps us from duplicating MP3/FLAC/OGG decode logic — foobar's built-in HTTP input does the heavy lifting.

- Album art is implemented as a full `album_art_extractor` (NOT `album_art_fallback`) on both platforms — macOS `NavidromeArtExtractor.mm`, Windows `NavidromeArtExtractorWin` in `NavidromePluginWin.cpp` (the WinHTTP fetch was already there via `NavidromeArtInstance` — it was just registered as `album_art_fallback` until this was caught). Returning true from `is_our_path()` guarantees foobar calls `open()` directly, which is more reliable than fallback for streamed content: `album_art_fallback` only runs after every other registered source has already declined, and in practice a cold `album_art_manager_v2` query (as done by e.g. a UI panel component asking for art of a track that isn't necessarily playing) throws "Attached picture not found" before fallback gets a chance — confirmed via a consuming component (`foo_ui_panels`) logging that exact exception for every `foo_navidrome` track while the Windows side still used `album_art_fallback`. `is_our_path` matches both `navidrome://` URIs (current) and legacy `/rest/stream.view` HTTP URLs (for old playlists). `open()` resolves the art id from, in priority order: `coverArt=` query param, `id=` query param, or the `<id>` segment of `navidrome://track/<id>`. The id is then passed to Subsonic's `getCoverArt.view` endpoint — which serves embedded ID3 art if present, otherwise the album folder's `Folder.jpg` / `cover.jpg`.

- **`.fb2k-component` multi-arch layout.** One zip ships every platform: macOS bundle under `mac/`, Windows x86 DLL at the **zip root**, x64 under `x64/`, native Windows-on-ARM under `arm64ec/`. foobar picks the binary at load time, preferring `arm64ec/` on ARM and falling back to x64 emulation if it's absent — so the arm build is a perf upgrade, not a correctness requirement. ARM target is **ARM64EC** (ABI-compatible with x64), NOT pure ARM64. Build adds a 3rd MSBuild target in `build-windows.yml`; `release.yml`'s merge-component folds the DLL in. Touching the arch set means updating both workflows AND the `ProjectConfiguration`/Configuration `PropertyGroup` list in `Windows/foo_navidrome.vcxproj` (only Win32/x64/ARM64EC defined; SDK projects already ship ARM64EC@v143). The windows-2022 runner preinstalls the v143 ARM64 tools + `VC.ATL.ARM64`, so no `vs_installer modify` is needed — but don't move off windows-2022 without re-checking that.

- Linux is intentionally unsupported (foobar2000 has no Linux build).

- **License: MIT for our code only.** `LICENSE` (MIT, © Santiago Rodriguez) covers this repo's source. The foobar2000 SDK + PFC are under the foobar2000 author's own terms, NOT MIT, and are not committed here (fetched at build time) — the README License section states this explicitly. Required for the official components listing: we can't re-license the SDK.

- The Subsonic salt is stored in `cfg_salt` with default `"fb2k_navidrome"` — kept configurable so token reuse can be invalidated if needed.

- GUIDs for all services live as `static constexpr GUID` in the namespace that registers them. The input handler's GUID is in `NavidromeInput.mm`; the others are in `NavidromePlugin.mm` (lines 11-18). Must be regenerated when forking the component.

## Gotchas

- **`install-macos.sh` must prefer Release over Debug builds.** DerivedData often contains a stale Debug `.component` from past Xcode builds. The script filters by `*/Products/Release/*` first, then sorts by mtime descending. If you change build configurations, audit `find_component()` in `install-macos.sh`.

- **`set -u` + empty bash arrays.** macOS ships bash 3.2 which treats `"${EMPTY_ARRAY[@]}"` as an unbound variable under `set -u`. `dev-build.sh` avoids this with explicit if/else around `./install-macos.sh` invocation rather than building an array of args. Same trap applies to any new shell helpers — either use explicit conditionals or `"${ARR[@]+"${ARR[@]}"}"`.

- **Bundle directory mtime doesn't update on `cp -Rf`.** When verifying an install, check the inner binary (`Contents/MacOS/foo_navidrome`), not the `.component` directory itself — `stat` on the directory shows the original mtime even after a fresh copy.

- **`library_viewer` surfacing on foobar2000 v2 Mac.** There is NO unified "Library viewers" list page in Preferences. Instead, each `library_viewer` is expected to register its own `preferences_page` parented to `preferences_page::guid_media_library` — that page becomes the entry point that appears under *Preferences › Media Library*, alongside Album List / ReFacets. Our component does both: a config page parented to `guid_tools` (credentials) AND a `guid_media_library` sub-page (`preferences_page_navidrome_library` in `NavidromePlugin.mm`) with an "Open Navidrome Browser" button + status. Users diagnosing "I don't see Navidrome in my Media Library" should check (1) foobar fully restarted, (2) *Preferences › Components* shows the component loaded OK, (3) `View › Console` for registration errors.

- **`library_viewer.get_preferences_page()` must point to the Media-Library-parented page**, not the Tools-parented one — that's how foobar wires the viewer into the Media Library navigation. In our code it returns `guid_library_prefs`, not `guid_prefs_page`.

- **Debugging input/decode failures.** There's no straightforward debugger attach for foobar2000 components on Mac, so logging is the primary diagnostic channel. Pattern: wrap each step of `decode_initialize` (URL resolution, nested `g_open_for_decoding`, nested `initialize()`) in try/catch and log both success and exception messages — the user-visible error ("Unsupported format or corrupted file") is a generic foobar translation that doesn't pinpoint the failing step. Remove the logs once the issue is identified to avoid noise in production.

- **Crash-resilient logging.** When foobar crashes mid-decode, `console::print` output gets lost (the console panel never flushes). Use a small file-logging helper (`navi_log` in `NavidromeInput.mm`) that writes to `/tmp/foo_navidrome.log` and closes the file after each line — that way the log survives even a hard crash. Mirror to `console::print` inside the helper for live viewing when nothing crashes.

- **`navidrome://track/<id>` URI parsing — host vs path trap.** RFC-3986 says `scheme://X/Y` puts `X` in the authority (host) component and `/Y` in the path. So `NSURLComponents` on `navidrome://track/abc123` gives `host=track`, `path=/abc123`. Splitting `path` on `/` yields only 2 components (`["", "abc123"]`), not 3 — `parse_uri` in `NavidromeInput.mm` strips the leading `/` from `percentEncodedPath` and uses that as the song id. The literal string `track` in the URI is part of the prefix-match check in `g_is_our_path`, not a path segment we parse out. If extending the scheme with more "sub-paths" (e.g. `navidrome://playlist/<id>`), make the dispatcher inspect `c.host` rather than path segments.

- **Any code that consumes `navidrome://` URIs must be updated when the scheme changes.** The art extractor's `is_our_path` originally only matched the legacy `/rest/stream.view` HTTP URLs and silently stopped working when we moved to `navidrome://` URIs — symptom was missing cover art in Now Playing with no error. Audit `strstr`/`strncmp` calls in `*.mm` whenever the URI scheme is touched.

- **`NSViewController` for dual-mount UIs in foobar2000.** Foobar's macOS preferences pages take an NSObject wrapped via `fb2k::wrapNSObject(...)`; passing an `NSViewController` works directly. To also expose the same UI as a standalone window (menu / `library_viewer.activate()`), create the VC and set `window.contentViewController = vc`. Do NOT share a single VC instance across multiple mount points — Cocoa requires each `NSView` to have a single superview, so a shared VC's `view` can only be parented to one host at a time. Each mount creates its own VC; data sharing happens at the model layer (`SubsonicClient` singleton).

- **Release-loop prevention.** semantic-release commits `version.txt` + `CHANGELOG.md` back to `main` after a successful release. Without guarding, that push would trigger the workflow again. Two layers of protection: (1) the release commit message contains `[skip ci]`, and (2) `release.yml` has `if: "!contains(github.event.head_commit.message, '[skip ci]')"` — both must remain in sync. The workflow also uses `concurrency: { group: release-${{ github.ref }} }` so two releases can't race on the same tag.

- **`dev-build.sh` vs `ci-build.sh` — keep them separate.** `dev-build.sh` is the local developer loop: bumps `version.txt`, builds, **installs locally** to `~/Library/foobar2000-v2/`, packages. `ci-build.sh` is for the GitHub Actions runner: receives the version as an arg (from semantic-release), builds into a hermetic `build/derived/` path, packages — and crucially does NOT touch `~/Library` (which on a runner would create a phantom dir). Don't merge them.

- **PCH compile fails on older Xcode with `std::string_view`/`std::string` errors.** `pfc/string-interface.h` uses `std::string` and `std::string_view` without `#include <string>` / `<string_view>` — it relies on transitive includes from libc++. On Xcode 26 + macOS 26 SDK those transitively land before pfc; on Xcode 15.4 + MacOSX14.5.sdk (the default on GitHub Actions `macos-14` runner) they do NOT, and the PCH compile of `stdafx.h` fails with two errors:
  ```
  pfc/string-interface.h:49: error: no type named 'string_view' in namespace 'std'
  pfc/string-interface.h:50: error: implicit instantiation of undefined template 'std::basic_string<char>'
  ```
  Fix is to add `#include <string>` and `#include <string_view>` to `stdafx.h` BEFORE `<helpers/foobar2000+atl.h>` — this guarantees both types are complete regardless of the toolchain. Don't try to "fix" pfc upstream; we don't control that fork.

- **`xcodebuild` log handling in CI.** Never truncate xcodebuild output with `| tail` — compile-command echoes scroll the actual `error:` lines off the top, so a failed PCH or single bad symbol shows up as just `** BUILD FAILED **` with no context. Two layers of observability:
  1. `ci-build.sh` redirects the full log to `/tmp/xcodebuild.log`, prints a filtered summary (`error:`/`warning:`/`note:`/`** BUILD`/`ld:`/`fatal:`/`FAILED`) on every run. On failure it dumps the full log and then re-prints `grep -B 3 "error:|fatal error:"` at the very end — GitHub Actions step output is read bottom-up when a job fails, so error lines at the tail are what the eye lands on.
  2. `release.yml` has an `if: failure()` step that uploads `/tmp/xcodebuild.log` as an artifact named `xcodebuild-log` (retention: 7 days), so anyone debugging can download and grep locally without scrolling through truncated GA logs.
  Don't switch to `xcpretty` or similar without preserving both layers — the diagnostic value when CI fails on a Mac build you can't reproduce locally is the whole point.

- **SDK source in CI: `reupen/foobar2000-sdk-unmodified`, not `marc2k3/foobar2000-sdk`.** `pfc/` is NOT a separate GitHub repo — `marc2k3/pfc` is a 404. We tried `marc2k3/foobar2000-sdk` (redirects to `javascript-panel/foobar2000-sdk`) — it contains `SDK/`, `helpers/`, `shared/`, `foobar2000_component_client/`, plus `pfc/`/`libPPUI/`/`columns_ui-sdk/` at the staging root — BUT it does NOT ship `helpers-mac/`, which we need for `NSView+embed.m`. `reupen/foobar2000-sdk-unmodified` is an unmodified mirror of the official SDK and does include `helpers-mac/`. Layout the workflow expects after checkout into `_sdk-staging/`:
  ```
  _sdk-staging/
    foobar2000/                       ← nested wrapper, NOT the target layout
      SDK/  helpers/  helpers-mac/  shared/  foobar2000_component_client/
    pfc/
    libPPUI/
    sdk-license.txt  sdk-readme.html
  ```
  The staging step moves `_sdk-staging/foobar2000/<dir>` → `foobar2000/<dir>` (SDK subdirs are one level deeper than the staging root) and `_sdk-staging/pfc` → `pfc` (this one IS at the staging root). The verify step requires `helpers-mac` in the list and prints `ls -la` of both `_sdk-staging` and `_sdk-staging/foobar2000` on failure so future layout drift is debuggable.

- **Testing the x64 build on ARM foobar (via `scripts/win-vm/`) has three traps** — all in `scripts/win-vm/README.md`, condensed here: (1) `clang-cl` parses a leading `/Users/...` source path as the `/U` flag → pass sources as `/Tp<path>`; (2) the local x64 build **must** use static CRT (`/MT`) — foobar-on-ARM only bundles the ARM64EC flavour of `VCRUNTIME140`/`MSVCP140`, so an emulated x64 `/MD` DLL fails to load silently; (3) install once from a `.fb2k-component` (it lands in `user-components-arm64ec/`), because a loose DLL dropped into a component folder is NOT picked up by the ARM build — after that the DLL can be hot-swapped in place. Also: the QEMU guest needs an audio device (`-device intel-hda`), else playback dies with "Element not found" (0x80070490) before decode.
