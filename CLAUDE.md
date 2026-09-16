# foo_navidrome

## Overview

foobar2000 v2 component: browse/stream from Navidrome / Subsonic-compatible servers. Cross-platform: macOS (shipped) and Windows x86/x64/ARM64EC (shipped). One `.fb2k-component` carries all platforms. Two entry points on macOS: **File › Open Navidrome Browser** and **Preferences › Media Library › Library viewers › Navidrome**.

## Working rule: every change is cross-platform

Any feature/fix touching shared or platform code must land on **both** macOS and Windows, not one — put logic in a shared file (`SubsonicTypes.h`, `SubsonicCore.h/.cpp`, `NavidromeBrowserModel.h/.cpp`, `main.cpp` SDK-only helpers) so both platforms consume the same implementation; only widget/wiring code stays platform-side. Checklist for any non-trivial change:
- **Both platforms**: implement/wire on macOS (`.mm`) and Windows (`Windows/*.cpp`) — don't ship one-sided.
- **Logging**: add `NAVIDROME_LOG`/`WARN`/`ERR` calls (see Gotchas) at meaningful steps (request sent, error, decision taken) — `scrubAuth()` any URL.
- **Tests**: add/extend a case in `tests/MediaEnrichmentLogicTests.cpp` for any shared/SDK-free logic — it's the one file all three toolchains run. Then **actually run `make test`** (or `scripts/run-unit-tests.sh win`) after any logic change — shared or platform-side — before calling it done, not just after adding a new case. A change that "should still pass" is unverified until the suite says so.
- **Make/scripts**: if a new build/test/deploy step is needed, wire it into the `Makefile` + the relevant script (`run-unit-tests.sh`, `win-build-local.sh`, `mac-dev-build.sh`, etc.), not just run ad hoc. Same for any command reached for more than once that's long or easy to forget the flags of (a build+launch combo, a multi-step VM flow, a symbolizer invocation) — turn it into a `make` target instead of re-typing/re-deriving it each time.
- **Docs**: update this CLAUDE.md (architecture/decisions/gotchas as relevant) and README when user-facing.

## Architecture

Shared C++ core + per-platform UI/HTTP layers:

- `main.cpp`, `stdafx.h`, `SubsonicTypes.h` — cross-platform shared code (version, pure-C++ data types, `trackIdFromURI()`, `StarKind`/`AlbumListType` enums)
- `SubsonicCore.h/.cpp` — shared Subsonic API core, SDK-free. Owns every request body: URL/auth assembly, retry loop, status-wrapper check, JSON→`navidrome::parseX`, music-folder cache, multi-library fan-out. Platform fills two seams: `IHttpTransport::getOnce()` (one GET, no retry) and `ISettingsProvider::load()`. Unit-tested (`testSubsonicCore`).
- `SubsonicClient.h/.mm` — macOS adapter over `SubsonicCore`: `MacHttpTransport` (NSURLSession) + `MacSettingsProvider`, raw byte paths for cover art/download, `navidrome::X` → ObjC `Subsonic*` marshalling.
- `Windows/SubsonicClientWin.h/.cpp` — Windows adapter over `SubsonicCore`: `WinHttpTransport` (WinHTTP) + `WinSettingsProvider`, plus Windows-only `httpGetBinary`/`httpDownloadToFile`. Singleton facade forwarding to `m_core`.
- `NavidromePlugin.mm` — macOS registration: `cfg_string` for URL/user/pass/salt, prefs page, File menu, `library_viewer` factory, scrobbler (`play_callback_static`).
- `NavidromeInput.mm` / `Windows/NavidromeInputWin.cpp` — `input_singletrack` handler for `navidrome://track/<id>?...` URIs. Metadata embedded in URI; HTTP stream URL resolved at decode time. Registered `input_entry::flag_redirect`, opens nested decoder via `g_open_for_decoding(fromRedirect=true)`.
- `SubsonicTypes.h` — also owns: URI codec (`TrackURI`, `buildTrackURI`/`parseTrackURI`, percent-encode/decode), `resolveArtId`/`rawQueryParam`, `scrobbleSubmitThreshold()`, the JSON parser (`json::Value`/`json::parse()`) + Subsonic mappers (`parseSong`/`parseAlbum`/etc.), `parseSubsonicResponse()`. Add any new SDK-free shared string/URL/parsing helper here.
- `NavidromePlaylistSync.h` (impl in `main.cpp`) — `syncRatingsToPlaylists()` pushes `NAVIDROME_RATING`/`NAVIDROME_STARRED` onto playlist entries via forced hints. `scanPlaylistAlbums()` collects `albumId=` values for startup refresh.
- `NavidromeBrowserModel.h/.cpp` — shared browser tree logic, SDK-free. Owns `navidrome::BrowserNode`, category order, model→node mappers, row-display formatting, and (behind `IBrowserClient`) `buildRootNodes()`, `fetchChildren()`, `collectSongsDeep()`, star/rate mutation, Play Similar/Random Mix fetch, `syncBrowserNodesToPlaylists()`. Platform supplies a thin `IBrowserClient` adapter (`WinBrowserClient`, `MacSubsonicBrowserClient`). Unit-tested (`testBrowserModel`, `testBrowserFetchDispatch`, `testStarRatingSimilarRandom`).
- `NavidromeBrowserEnqueue.h` (impl in `main.cpp`) — `enqueueBrowserNodes()`: builds URIs, pushes metadb hints, appends/clears playlist, starts playback honoring Playback › Order, resumes bookmarks via `seekWhenReady()`.
- `NavidromeArtExtractor.mm` — `album_art_fallback` for Navidrome-fetched art.
- `Mac/NavidromeBrowserController.*` — browser `NSViewController`, mounted 3 ways (prefs sub-page, standalone window, dockable `ui_element_mac_navidrome`); each mount creates a fresh instance (Cocoa: one superview per NSView).
- `Mac/NavidromePreferencesController.*` — prefs UI.
- `Windows/NavidromePluginWin.cpp`, `Windows/BrowserWindow.*` — Windows ATL equivalents. `BrowserWindow` is a thin Win32 view over `NavidromeBrowserModel` via `WinBrowserClient`. Mounts as standalone window or embedded `WS_CHILD` panel. Context menu on right-click.
- `Windows/MediaEnrichmentLogic.h/.cpp` — SDK-free helper (URI/URL codec, cover-art URL, HTTP classification, LRU `CoverCache`, ESLyric config gen). Windows-only consumer; tests build cross-platform. One platform-specific line: MD5 (`#if _WIN32` WinCrypt else CommonCrypto).
- `Windows/EsLyricBridge.h/.cpp`, `Windows/EsLyricScript.h` — bridges to third-party [ESLyric](https://github.com/esdatura/eslyric-fb2k). Writes `scripts/lib/foo_navidrome/config.js` + searcher script on startup/credential-save. No-op if ESLyric not installed.

GUIDs for cfg vars/prefs pages/menu commands are hardcoded constants in `NavidromePlugin.mm` (lines 17–30) — regenerate when forking.

### Subsonic feature surface (both platforms)

| Feature | Endpoint(s) | Surfaced as |
|---|---|---|
| Scrobbling | `scrobble.view` | `play_callback_static`; `submission=false` on new track, `true` at `min(240s, length/2)`; gated on `cfg_scrobble` |
| Smart lists | `getAlbumList2.view`, `getStarred2.view` | Category nodes above artist list |
| Server playlists | `getPlaylists.view`, `getPlaylist.view` | "Playlists" category node |
| Playlist upload/CRUD | `createPlaylist.view`, `updatePlaylist.view`, `deletePlaylist.view` | Context menu items |
| Genres | `getGenres.view`, `getSongsByGenre.view` | "Genres" category node |
| Favorites | `star.view`/`unstar.view` | Star/Unstar menu, `★` prefix |
| Ratings | `setRating.view`, `getSong.view`, `getAlbum.view` | Rating submenu; exported to playlists as `NAVIDROME_RATING`/`NAVIDROME_STARRED` |
| Transcoding | `stream.view` `format`/`maxBitRate` | Two prefs combo boxes; option lists in `navidrome::streamFormatOptions()`/`maxBitrateOptions()` |
| Download originals | `download.view` | Context menu, folder picker, never transcoded |
| Bookmarks | `getBookmarks.view`, `createBookmark.view`, `deleteBookmark.view` | File-menu bookmark, "Bookmarks" category, resume on play |
| Internet radio | `getInternetRadioStations.view` + CRUD | "Radio" category node + dedicated prefs sub-page |
| Podcasts | `getPodcasts.view` (list + episodes), `createPodcastChannel.view`, `deletePodcastChannel.view` | "Podcasts" category node → channel nodes → episode nodes; Subscribe/Unsubscribe context menu (no update endpoint) |
| Now Playing | `getNowPlaying.view` | "Now Playing" category node, songs annotated with `user · Nm ago` |
| Library scan | `startScan.view`, `getScanStatus.view` | "Rescan Library Now" button, polls every `kScanPollIntervalMs` (1.5s) |
| Play Similar | `getSimilarSongs2.view` | Context menu item, any row |
| Random Mix | `getRandomSongs.view` | Context menu item (not a category node — see Gotchas) |
| Multi-library filter | `getMusicFolders.view`; `musicFolderId` on list endpoints | *Preferences › Media Library › Navidrome › Libraries*; fan-out+merge when filter active |

Playlist mutations chunk **50 ids/request** (`kPlaylistChunkSize`) — Windows' URL buffer is 4096 wchars.

`getGenres.view` reports name in field **`value`**, not `name` — no genre id, so `NavidromeNode.id` for a genre row is the genre string.

Credentials persist via `cfg_string`. Password sent via Subsonic token auth (md5 of password+salt).

## Development

`Makefile` wraps scripts as `make` targets (`make help`). `<os>-<action>` scheme. E.g. `make test` (fast Linux clang-cl+wine unit tests), `make mac-test`, `make win-build`/`win-install`, `make win-build-launch` (build+install+relaunch Wine foobar), `make mac-build`/`mac-release`, `make win-vm-*`, `make mac-vm-*`.

### Build layout (siblings required)
```
foobar2000/
  SDK/, helpers/, shared/, foobar2000_component_client/
  foo_navidrome/        ← this repo
pfc/                    ← sibling of foobar2000/
```

### macOS
Open `foo_navidrome.xcworkspace` (not the bare xcodeproj) → build `foo_navidrome` scheme → `./scripts/install-macos.sh` → restart foobar2000.

### Windows
VS2022, `Windows/foo_navidrome.vcxproj` (update `<ProjectReference>` GUIDs to local SDK). Build Release|x64, copy `.dll` to `%APPDATA%\foobar2000\user-components\foo_navidrome\`.

### Unit tests (`tests/`, one source, three toolchains)
`tests/MediaEnrichmentLogicTests.cpp` — standalone console exe, no SDK. Covers `SubsonicTypes.h` + `Windows/MediaEnrichmentLogic.cpp`. `scripts/run-unit-tests.sh [mac|win|auto]` is the source of truth for the compile+run command.
- Windows/MSVC: `tests/MediaEnrichmentTests.vcxproj` (CI runs every time, doesn't use the script).
- macOS: `make mac-test` → plain clang++ -Wall -Wextra -Werror.
- Linux: `make test` → clang-cl + xwin + wine (no foobar SDK siblings needed).

Both local build scripts (`win-build-local.sh`, `mac-dev-build.sh`) gate on tests (`--no-test` to skip). Add new assertions once to `tests/MediaEnrichmentLogicTests.cpp` — all hosts pick it up.

### Windows testing on macOS (no PC/Wine) — `scripts/win-vm/`
Cross-compiles x64 DLL on macOS, runtime-tests in headless Windows 11 ARM64 QEMU/HVF guest. `setup-mac-toolchain.sh` → `fetch-win11-arm.sh` → `win-vm.sh install` → `win-vm-test.sh --launch`. See `scripts/win-vm/README.md`. ARM64EC needs MSVC (clang can't cross-compile it) — CI builds the real ARM64EC binary; the x64 build here runs emulated, fine for UI testing.

### macOS testing on Linux (no Mac) — `scripts/mac-vm/`, Docker-OSX
Run/test only, no build (no Xcode in container). **`make mac-vm`** is the one-shot orchestrator (`mac-vm-up.sh`): host preflight → detached VNC container (`:snap` if exists) → wait SSH → auto-commit `:snap` on first success → auto-install foobar2000 → test/launch. Manual step (unavoidable, no unattended macOS installer exists): the *first* macOS install click-through over VNC — do once, then `make mac-vm-snapshot-export` for a portable `.tar.zst`, `mac-vm-snapshot-import FILE=…` restores anywhere. `make mac-vm-reinstall` wipes + rebuilds clean. See `scripts/mac-vm/README.md` for the granular targets and full trap list (guest is emulated Intel — needs x86_64 slice; Wayland+NVIDIA host needs `MAC_VM_VNC=1`; re-sign in guest mandatory after deploy; never `docker attach` — hangs on `-monitor stdio`).

### Versioning + dev loop
Single source of truth: `version.txt`. Xcode's "Generate Version Header" phase reads it every build → `version_generated.h` (gitignored).
```bash
./scripts/mac-dev-build.sh                # bump patch, build, install
./scripts/mac-dev-build.sh --minor/--major/--no-bump/--no-install/--new-release
```
`win-build-local.sh` mirrors this (writes `version_generated.h`, same bump flags) but **defaults to no bump** (the Linux cross-compile loop runs constantly and `version.txt` is tracked). Symmetric `make {win,mac}-build-{patch,minor,major}` targets.

### Release pipeline
`.github/workflows/release.yml` on push to `main`, via [semantic-release](https://semantic-release.gitbook.io/) reading [Conventional Commits](https://www.conventionalcommits.org/) (config `.releaserc.json`):
- `feat:` → minor · `fix:`/`perf:`/`refactor:` → patch · `chore:`/`docs:`/`style:`/`test:`/`ci:` → no release · `!`/breaking footer → major.
- **`commit-analyzer` and `release-notes-generator` type lists must stay in sync** — the `conventionalcommits` preset hides `refactor`/`docs`/`build` by default; any type added to `releaseRules` with `release != false` needs a matching non-hidden entry in `release-notes-generator`'s `presetConfig.types` or its notes are empty.
- Chain: `commit-analyzer` → `release-notes-generator` → `changelog` → `exec` (`mac-ci-build.sh <version>`, xcodebuild + package `.fb2k-component`) → `git` (commits `version.txt`+`CHANGELOG.md`, `[skip ci]`) → `github` (release + asset).
- SDK cloned from `marc2k3/foobar2000-sdk` + `marc2k3/pfc` into sibling layout at CI time.
- **`release.yml` only checks the push's top commit type** — a `chore:` HEAD skips release even with a `feat`/`fix` underneath it.
- First-time setup needs a starting tag (`git tag v$(cat version.txt) && git push --tags`) or semantic-release treats next release as v1.0.0.

### Manual release (bypasses CI)
```bash
./scripts/mac-dev-build.sh --new-release
```

## Decisions & Constraints

- **Media Library integration is native via custom URI + `library_viewer`.** foobar's Music Folders only accepts filesystem paths — no SDK hook for a remote source. Two SDK extensions: `input_singletrack` for `navidrome://track/<id>?...` (metadata in URI so playlists render without a network round-trip; stream URL resolved at decode time so playlists survive credential rotation), and `library_viewer` registration.

- **Embedded browser: three mount points, one `NSViewController` class**, each mount creating a fresh instance (Cocoa requires one superview per NSView). Data sharing happens at the model layer (`SubsonicClient` singleton), not the VC.

- **Browser logic is shared C++; the platform layer is just the view.** `NavidromeBrowserModel.h/.cpp` + `NavidromeBrowserEnqueue.h` own tree model, category list, child fetch, deep-collect, row-label formatting, playlist rating push-back, star/rate mutation, Play Similar/Random Mix, enqueue/seek. `BrowserWindow`/`NavidromeBrowserController` keep only widget wiring, theming, menus, threading, and their `IBrowserClient` adapter. Add browser behaviour to the shared core, not a platform file.
  - macOS keeps ObjC `NavidromeNode` as a thin view-model bridged via `+wrapCoreNode:`/`-coreNode`; `NavidromeNodeType`/`NavidromeCategoryKind` must stay declared in the same order as the C++ enums (bridged by cast).

- **Enter in the browser tree replaces the active playlist; double-click/Add/Play still append.** `clearFirst` bool threaded through both platforms' enqueue chain, `true` only on the Enter/`NM_RETURN` path (clears the active playlist only, nothing else).

- **Album art is a full `album_art_extractor`, not `album_art_fallback`** — guarantees `open()` is called, more reliable for streamed content. `is_our_path` matches `navidrome://` URIs and legacy `/rest/stream.view` URLs (old playlists). Art id priority: `coverArt=` → `id=` → `<id>` segment of the URI.

- **`.fb2k-component` multi-arch layout:** macOS bundle under `mac/`, Windows x86 at zip root, x64 under `x64/`, ARM64EC under `arm64ec/`. foobar prefers `arm64ec/` on ARM, falls back to x64 emulation. ARM target is ARM64EC (x64-ABI-compatible), not pure ARM64. Touching the arch set = update both workflows + the vcxproj's ProjectConfiguration list.

- Linux is intentionally unsupported (no foobar2000 Linux build).

- **Multi-library filter is client-side fan-out** ([issue #9](https://github.com/santiagorod92/foo_navidrome/issues/9)): Subsonic endpoints take one `musicFolderId`, never a list, so a subset selection = one request per selected id, merged+de-duped by item id. Shared kernels in `SubsonicTypes.h`: `appendMusicFolderParam`, `mergeFanOut<T>`. `getAlbum`/`getSong`, playlists, bookmarks, radio, `getSimilarSongs2` untouched (already id-scoped). **`getAlbumsForArtist` needs a second call:** `getArtist.view` ignores `musicFolderId` server-side, so when the filter is active it fans `search3.view` (which honours it) and filters via `filterAlbumsByArtistSearch()`; empty allow-set falls back to the unfiltered list + a `NAVIDROME_WARN` log. Whole decision funnels through `effectiveMusicFolderIds()` → `{}` means "one request, today's behaviour" (filter off, empty selection, `<2` server folders, or selection covers every folder). Config: `cfg_library_filter` (bool) + `cfg_library_ids` (csv), GUID tails `0x10`/`0x11`. Windows stages edits (Cancel discards); **Mac writes live on every click** (no apply/reset hook via `fb2k::wrapNSObject`) — true of every Mac prefs page in this component.
  - **A multi-library server groups the browser tree by library independent of the filter checkbox** — gate is `libraryGroupingIds()` (server has ≥2 libraries logic), not `activeMusicFolderIds()` (collapses to `{}` in fan-out-no-op cases where grouping must still happen). ≥2 grouping ids → one Library node per id, lazily expanded via `getArtistsForLibrary(id)`; artist nodes carry `libraryId` for scoped album fetch. Category nodes stay merged across libraries.

- **Windows theming has two independent axes** ([issue #4](https://github.com/santiagorod92/foo_navidrome/issues/4)): Dark Mode (native chrome, `fb2k::CCoreDarkModeHooks`, `m_darkMode.AddDialogWithControls(*this)` in `OnCreate`) and Colours/Fonts (`ui_config_manager::query_color`, works from a plain window per `CListControlFb2kColors`; `BrowserWindow::refreshThemeColors()` + `ui_colors_changed()` callback for live updates). Native push buttons can't be recolored (Win32 ignores `WM_CTLCOLORBTN` for `BS_PUSHBUTTON`). Any new top-level/embedded window needs both hookups.

- **License: MIT for our code only.** SDK + PFC are under the foobar2000 author's own terms, not committed here.

- Subsonic salt stored in `cfg_salt`, default `"fb2k_navidrome"`.

- GUIDs live as `static constexpr GUID` where registered (input handler in `NavidromeInput.mm`, rest in `NavidromePlugin.mm` lines 17-30). Regenerate when forking.

## Gotchas

- **JSON responses are a real DOM (`navidrome::json`, `SubsonicTypes.h`) — walk the full path**, e.g. `root["searchResult3"]["album"].items()`. No substring key scanning (the old Windows scanner matched a key anywhere in the body, giving false hits through nesting levels). `.items()` unwraps Subsonic's single-element-array-collapsed-to-object. `checkResponse` returns Null on failure (`.isNull()`). Parser rejects trailing content after the top-level value.

- Every `SubsonicClient.h` model class needs a matching `@implementation` in `SubsonicClient.mm`, even if empty — an `@interface` alone compiles but fails at **link** time (`Undefined symbols ... _OBJC_CLASS_$_...`), not compile time.

- **`MediaEnrichmentLogic.cpp` / `EsLyricBridge.cpp` / `NavidromeBrowserModel.cpp` / `SubsonicCore.cpp` must stay `<PrecompiledHeader>NotUsing</PrecompiledHeader>`** in the vcxproj — the first three deliberately skip `stdafx.h` (kept SDK-free for standalone test builds); `EsLyricBridge.cpp` includes it but is still marked NotUsing.

- The unit-test host stubs `navidrome::syncRatingsToPlaylists` as a no-op (SDK-only, lives in `main.cpp`). A new shared-browser-core call into an `main.cpp`-only symbol needs the same stub or an `IBrowserClient`-seam alternative.

- **Transcoding changes the codec — the decoder hint must follow.** Both input handlers pass the suffix through `navidrome::effectiveStreamSuffix()` before building the `track.<suffix>` hint (`"raw"`/`""` = original file, leave suffix alone).

- **`songIndexToRemove` is positional, evaluated per request** — send removals highest-index-first, or earlier removals shift later indices (silent wrong-track deletion at >50 removals, where chunking makes it worse).

- A `preferences_page`'s `get_parent_guid()` can point at another page's own GUID (not just an SDK branch GUID) to nest as a visible child — used by Radio Stations (parent = main Navidrome page's GUID, tail `0x05`).

- **Resume-from-bookmark needs a "seek when ready" poll** — `playback_seek()` can't be called right after `start()`. Both platforms poll `playback_can_seek()` on a background thread (~100ms/~3s timeout) then seek on the main thread. Reuse `BrowserWindow::seekWhenReady` for future seek features. Bookmark `position` from server is **ms**; `playback_control` is **seconds** — convert at the boundary.

- **Radio stations bypass the `navidrome://` scheme entirely** — raw `streamUrl` set directly as `playable_location_impl`, played by foobar's stock HTTP input. No custom headers reach a station stream; that's inherent to Subsonic radio, not a bug.

- `createInternetRadioStation.view` never echoes the new id back — both clients always return `""` on success; callers must check the error output, not treat empty-return as failure.

- **Random Mix is a context-menu action, not a category node** — a category node above the artist list never rendered under Wine (root cause never pinned down, emoji-in-title theory ruled out). If a future smart-list category node silently fails to render, don't assume the same cause.

- **A category/tree node's children only render after a real `fetchChildren` round-trip** — both platforms' expand handlers (`OnTreeExpanding` on Windows, `outlineViewItemWillExpand` on Mac) skip entirely once `childrenLoaded` is already true, and the actual tree-item insertion only happens from that fetch's completion callback. Pre-populating `node.children` ahead of time (e.g. to save a request) renders nothing — this is why Podcasts is a genuine two-level lazy tree (`CatPodcasts` → channel list → each channel's own expand fetches its episodes) instead of one `getPodcasts.view?includeEpisodes=true` call.

- **Podcast episode playability is gated by a bare id, not a separate flag** — `makePodcastEpisodeNode()` only sets the node's `id` to the episode's `streamId` once the server reports `status == "completed"`; otherwise `id` stays empty. `collectSongIdsDeep()` already drops id-less nodes (`NavidromeBrowserModel.cpp`), so an undownloaded episode is automatically un-enqueueable with no platform-side special-casing — `infoText` just shows the status so the user knows why.

- **`make win-build`/`win-build-local.sh` doesn't restart Wine foobar2000** — a replaced DLL under a still-running process is silently ignored until restart. Use `make win-build-launch`.

- **`win-build-local.sh`'s object cache is keyed on mtime only, not flags/headers.** Editing a widely-`#include`d header (`SubsonicClientWin.h`, `SubsonicCore.h`, `NavidromeBrowserModel.h`, `SubsonicTypes.h`) without `--clean` can ship a stale-ABI DLL that crashes at runtime with no diff to explain it (confirmed twice, once via member-layout change, once via the `SubsonicCore` facade refactor — see [[windows-build-cache-header-staleness]] memory). Symptom: crash with offset but no code-side explanation → try `--clean` before chasing a phantom bug. Symbolize with `llvm-symbolizer --obj=foo_navidrome.dll --relative-address -f -C <offset>` against a `/Z7 /debug:full` rebuild (debug-only flags, don't leave them in — ~25% size bloat).

- **Reading the tree control off the UI thread is a cross-thread call.** Every background action captures selection on the UI thread first, passes the node vector into the worker (`collectSongIdsDeep` takes pre-captured nodes). Same rule for macOS `-selectedNodes`/`-parentForItem:`.

- `cfg_int`/`cfg_bool` must be qualified `cfg_var_modern::cfg_int`/`cfg_bool` — unqualified resolves to the legacy variant (no `set()`, different serialization) on Windows SDK headers; Windows fails to build, macOS compiles fine. `cfg_string` has no such ambiguity.

- **`std::min`/`std::max` need parens on Windows:** `(std::min)(a, b)` — `windows.h` macros `min`/`max` otherwise mangle the call. macOS compiles the bare form fine.

- **Adding a `Windows/*.cpp` only needs a `Windows/foo_navidrome.vcxproj` edit** — both clang-cl build scripts derive their source list from the vcxproj via `scripts/component-sources.sh`. A *moved/renamed* file still needs the vcxproj edit, and if it leaves `Windows/`, check `component-sources.sh`'s `../`-relative handling. Tested helpers also need `tests/MediaEnrichmentTests.vcxproj` + `run-unit-tests.sh`.

- **Cover-art cache (`CoverCache`) and ESLyric config need explicit invalidation on credential/header changes** — keyed from server URL+user+token at last save, not read live. `NavidromePluginWin.cpp` clears/reinstalls both in `NavidromeHeadersWindow::OnSave` and `NavidromePrefsInstance::apply()`. Any new place credentials change needs the same two calls.

- **`install-macos.sh` must prefer Release over Debug** — DerivedData often has a stale Debug `.component`; `find_component()` filters `*/Products/Release/*` first, then sorts by mtime.

- macOS ships bash 3.2: `"${EMPTY_ARRAY[@]}"` under `set -u` is an unbound-variable error. Use explicit if/else or `"${ARR[@]+"${ARR[@]}"}"`.

- **Never run `mac-dev-build.sh`/`install-macos.sh` while foobar2000 is running** — replacing/re-signing the `.component` under a live process causes `EXC_BAD_ACCESS (SIGKILL (Code Signature Invalid))` on the next touched page, with a crash report pointing at unrelated code. Tell it apart from a real crash: termination namespace is CODESIGNING, fault is at offset `+0` of some function. Quit foobar2000 first.

- Bundle directory mtime doesn't update on `cp -Rf` — check the inner binary (`Contents/MacOS/foo_navidrome`) mtime when verifying an install.

- **No unified "Library viewers" list page on foobar2000 v2 Mac** — each `library_viewer` registers its own `preferences_page` parented to `guid_media_library` to get an entry point there. `library_viewer.get_preferences_page()` must return that Media-Library-parented GUID, not the Tools-parented one.

- **No debugger attach for foobar2000 components (Mac or Wine)** — logging is the primary diagnostic channel. `NAVIDROME_LOG`/`WARN`/`ERR` macros in `NavidromeDebugLog.h`, gated on `NAVIDROME_DEBUG_LOG`, write `foo_navidrome_debug.log` to `/tmp` (crash-resilient: `fopen`/`fprintf`/`fclose` per line). `make win-logs`/`make mac-logs` tail it. Tags: `UI`, `HTTP`, `API`, `Input`, `Scrobble`, `Rating`, `Bookmark`, `Env`, `Playlist`, `Art`. Runtime knobs (read once, no rebuild): `NAVIDROME_LOG_LEVEL`, `NAVIDROME_LOG_TAGS`, `NAVIDROME_LOG_MAX_MB` (default 8, truncates at start if over). **Any new line printing a URL must call `navidrome::dbg::scrubAuth()` first** — redacts `t`/`s`/`p`/`u` query values (easy to miss on the raw decode hint). `navidrome::dbg::runGuarded(tag, what, fn)` wraps detached-thread bodies in try/catch (active in every build, not just debug — an escaping exception there is `std::terminate`). Header-only, zero-cost when the flag is off; not added to any build source list.

- **Unified HTTP error model:** `navidrome::ErrorKind`/`Error` in `SubsonicTypes.h`. `isRetryable()` covers `Network`/`Timeout`/`RateLimited`/`ServerError`; every retry loop on both platforms shares the same policy via `navidrome::retry` (`kMaxAttempts`=3, `again()`, jittered backoff `backoffMs()` = `300ms·n + jitter[0,200)`) — never reimplement a local max-attempts constant or backoff formula, share the namespace instead. `Auth` outcomes print a once-per-session console warning (`warnAuthOnce()`/`NavidromeWarnAuthOnce()`). The main JSON request path retries inside `SubsonicCore.cpp`'s loop (jitter from `IHttpTransport::jitterMs()`, a default `std::mt19937` impl neither platform overrides). Outside that path, the two platforms diverge by design: Windows' `httpGetBinary`/`httpDownloadToFile` (WinHTTP, `WinHttpHandle` RAII wrapper) get RAII+logging but no retry — cover art is cached/best-effort, downloads are user-initiated with visible errors; macOS's `-dataForURL:` (the separate bare-byte cover-art fetch) *does* retry, but through the same `navidrome::retry` primitives via the client's own `_transport` — no local reimplementation.

- **`navidrome://track/<id>` parse/build is one shared implementation** (`SubsonicTypes.h`). Building has exactly one caller (`enqueueBrowserNodes` in `main.cpp`); input handlers only parse. Plain string scan, not `NSURLComponents` (which mis-parsed `track` as authority) — future scheme growth (e.g. `navidrome://playlist/<id>`) should branch on the segment right after `navidrome://`.
  - **Encoder is strict RFC-3986** (unreserved chars only) — a Mac-built URI for a track with `! * ' ( )` in title/artist changes byte-for-byte vs the old `NSURLQueryAllowedCharacterSet` encoding. Since the URI is the `playable_location`, that's a one-time cosmetic dedup miss on upgrade for affected rows, not data loss.

- **`get_info()` is NOT the display path for a freshly enqueued track — the metadb hint list is.** Any field added to `get_info()` must also go in the hint block (`file_info_impl` pushed via `metadb_hint_list`) or it stays blank until a forced info reload. `NAVIDROME_RATING`/`NAVIDROME_STARRED` are set in both places for this reason.

- **Don't name an exported field `rating`** — collides with foobar's own Playback Statistics `%rating%` provider (SDK warns overlaying a standard field has "severe ill effects", undefined precedence). Server values export under `NAVIDROME_` prefix. Same reasoning for any future exported field.

- **A URI-embedded value can only be refreshed through metadb, never by rewriting the URI** — a changed query param is a new `playable_location`, i.e. a duplicate playlist entry, not an update. `syncRatingsToPlaylists()` uses `metadb_hint_list_v3::add_hint_forced()` (forced because a normal hint is skipped when the file hasn't changed by timestamp, which a URI never does). Consequence: metadb and the URI now disagree, so a user-forced full info reload regresses to the enqueue-time snapshot — never to garbage.

- **The rating refresh must not sit behind `cfg_scrobble`** — it's a display concern, not a play report. `navidrome::ScrobbleTracker::onNewTrack()` returns `{refreshRatingId, scrobbleNowId}` separately; `refreshRatingId` fires for every one of our tracks regardless of the scrobble pref.

- **Startup refresh groups by album** — `getAlbum.view` returns `userRating` for every song in one request, collapsing the pass to one request per distinct album (`getSong.view` has no multi-id form). That's the only reason `albumId=` is in the URI; the input handler never reads it back. Entries written before this existed can't be grouped and are skipped (counted, reported, never swallowed) — they still catch up via rate/star, browse, playback, or a sibling entry from the same album.

- **`init_stage_callback`/`FB2K_ON_INIT_STAGE` never dispatches on macOS** — silent no-op, no error, no console output. Use `initquit::on_init` instead (fires both platforms; playlists are already loaded by then).

- **Report skipped coverage, stay quiet about user-chosen states.** Startup refresh has four no-op paths: switch off / no server configured (user's choice, don't log every start) vs. no album id / skipped-after-partial-run (leaves entries behind, must log counts).

- **Every context-menu action's failure branch must `NAVIDROME_WARN("UI", …)`, not just `setStatus`/error label.** Star/unstar, rate, and playlist CRUD (add/create/remove/rename/delete, send active playlist) on both platforms log the error string when the underlying call fails — `setStatus`/`_statusLabel` alone is invisible once the window closes, and it's the only record when a user reports "X didn't work" after the fact. Still missing this on both platforms: bookmark removal, radio station CRUD. Add the same one-liner (op name + target + error) when touching those.

- Startup refresh's off switch is an `advconfig_checkbox_factory` (*Preferences › Advanced › Tools*), not a `cfg_bool` — avoids UI work on both prefs dialogs and the `cfg_var_modern::cfg_bool` qualification trap.

- **Any code consuming `navidrome://` URIs must be updated when the scheme changes** — the art extractor's `is_our_path` once only matched legacy `/rest/stream.view` URLs and silently broke (missing cover art, no error) when the scheme moved to `navidrome://`. Audit `strstr`/`strncmp` calls in `*.mm`/`*.cpp` whenever the URI scheme is touched.

- **`NSViewController` multi-mount pattern:** foobar's Mac prefs pages and `ui_element_mac::instantiate()` both just take a `fb2k::wrapNSObject`-wrapped `NSViewController`. Never share one VC instance across mount points (Cocoa: one superview per view) — each mount creates its own; data sharing is at the model layer. `ui_element_mac` may call `instantiate()` more than once per session (dock/undock/split) — never cache a VC across calls.

- **Release-loop prevention:** semantic-release's release commit has `[skip ci]`; `release.yml` also gates on `!contains(head_commit.message, '[skip ci]')` and uses `concurrency: { group: release-${{ github.ref }} }`. Both must stay in sync.

- **`mac-dev-build.sh` vs `mac-ci-build.sh` — don't merge them.** Dev script bumps version + installs to `~/Library/foobar2000-v2/`; CI script takes version as an arg, builds into hermetic `build/derived/`, never touches `~/Library`.

- **PCH needs `#include <string>`/`<string_view>` in `stdafx.h` before `<helpers/foobar2000+atl.h>`** — `pfc/string-interface.h` relies on transitive includes that Xcode 15.4/macOS14 SDK (GH Actions `macos-14` runner) doesn't provide, unlike newer toolchains. Don't try to fix this in pfc upstream (not our fork).

- **Never pipe `xcodebuild` output through `tail` in CI** — scrolls the actual `error:` lines off. `mac-ci-build.sh` redirects full log to `/tmp/xcodebuild.log`, prints a filtered summary, and on failure re-prints `grep -B3 error:` at the very end (GH Actions reads failed-job output bottom-up). `release.yml` also uploads the full log as an artifact on failure.

- **SDK source in CI is `reupen/foobar2000-sdk-unmodified`, not `marc2k3/foobar2000-sdk`** — the latter (redirects to `javascript-panel/foobar2000-sdk`) is missing `helpers-mac/`, needed for `NSView+embed.m`. Staging layout: `_sdk-staging/foobar2000/<dir>` → `foobar2000/<dir>` (one level deeper), `_sdk-staging/pfc` → `pfc` (at staging root).

- **`scripts/win-vm/` x64-on-ARM traps:** (1) `clang-cl` misparses a leading `/Users/...` path as `/U` flag → pass sources as `/Tp<path>`; (2) local x64 build must use static CRT (`/MT`) — ARM foobar only bundles ARM64EC runtime DLLs, an emulated x64 `/MD` DLL fails to load silently; (3) install once from a `.fb2k-component` (lands in `user-components-arm64ec/`) — a loose DLL in a component folder isn't picked up by the ARM build, but can be hot-swapped after; (4) QEMU guest needs `-device intel-hda` or playback dies before decode.

- **`scripts/mac-vm/` (Docker-OSX) traps:** x86_64 slice required (emulated Intel guest — universal CI build works, local arm64-only `mac-dev-build.sh` output doesn't); macOS install is manual (no unattended-install equivalent, Remote Login must be enabled by hand once); GUI transport + SSH port are fixed at `docker create` time (changing means `rm`+`install` again); guest bundle must be re-signed after `tar` deploy (`codesign --sign - --force --deep`) or macOS SIGKILLs it; disk is the container's writable layer — snapshot (`docker commit`, minutes-long, verify before any `rm`) before risky changes; only `sickcodes/docker-osx:latest` on Docker Hub (version picked via `SHORTNAME` env); on Wayland+NVIDIA hosts use `MAC_VM_VNC=1` (X11 passthrough needs a GL stack the NVIDIA proprietary driver can't provide); never `docker attach` (hangs on `-monitor stdio`); read the VNC screen to diagnose a stuck boot (stdout only carries QEMU's own logs, never the guest framebuffer).
