# foo_navidrome

A [foobar2000](https://www.foobar2000.org/) component that lets you browse and stream music from a [Navidrome](https://www.navidrome.org/) server (or any [Subsonic](http://www.subsonic.org/)-compatible server) directly inside foobar2000.

## Installation (end users)

1. Download `foo_navidrome_<version>.fb2k-component` from the [Releases](../../releases) page. It's a **single multi-platform component** — one file ships macOS, Windows x86, Windows x64, and native Windows-on-ARM (ARM64EC). foobar2000 picks the right binary at load time.
2. Drag the file onto foobar2000, or double-click it — foobar2000 installs it automatically
3. Restart foobar2000
4. Go to **Preferences › Tools › Navidrome** and enter your server URL and credentials

## Features

- Browse your entire music library: Artists → Albums → Songs
- **Smart lists** at the top of the tree: ★ Starred, Recently Added, Most Played, Recently Played, Random Albums, 🔀 **Random Mix** (a fresh batch of random tracks), **Genres**, and your **server-side playlists**
- **Scrobbling**: plays are reported back to Navidrome, so play counts, "Recently Played" and any Last.fm / ListenBrainz relay the server has configured stay in sync (toggle in Preferences)
- **Favorites and ratings** from the right-click menu — Star / Unstar and a 0-5 star rating, stored per-user on the server so they show up in the web UI and on your phone, and available in the playlist as `%navidrome_rating%` / `%navidrome_starred%` for a custom column. Also available straight from **the playlist's own right-click menu** (rate/star a track without going back to the browser), and kept in sync in already-added playlist entries — see [Showing ratings in the playlist](#showing-ratings-in-the-playlist)
- **Manage server playlists** from the right-click menu — add the selection to any existing playlist or a **New Playlist…**, remove tracks, rename, delete. Changes land on the server, so they show up everywhere
- **Send Active Playlist to Navidrome** from the right-click menu — uploads the active foobar2000 playlist under the same name
- **Bookmarks (resume playback)** — bookmark the currently playing track's position from the File menu, browse them under the **Bookmarks** smart list (shows a trailing `⏱ m:ss`), remove one from its right-click menu. Backed by Subsonic's `getBookmarks.view` / `createBookmark.view` / `deleteBookmark.view`
- **Internet radio stations** — browse/play stations under the **Radio** smart list, or manage them (New/Edit/Delete) from the tree's right-click menu or the dedicated **Preferences › Media Library › Navidrome › Radio Stations** sub-page
- **Streaming quality**: ask the server to transcode on the fly (MP3 / Opus / AAC) and cap the bitrate — useful on slow links. Set in Preferences; "Original" always sends the stored file
- **Download Original Files…** from the right-click menu — saves the selected tracks to a folder, always in their stored format regardless of the streaming setting
- Add albums or artists to playlist in one click (loads all songs automatically)
- Right-click any row for a **Play Now / Add to Playlist** context menu
- **Play Similar** from the right-click menu — queues and plays last.fm-derived recommendations for the selected artist, album, or song (`getSimilarSongs2.view`)
- Double-click a song to play immediately
- Live search across artists, albums and songs — results update as you type (debounced, no per-keystroke server hammering)
- **Rescan Library Now** button in Preferences › Tools › Navidrome — triggers a server-side scan and shows live progress, for when files were added/removed server-side and you don't want to wait for Navidrome's own scan schedule
- Album artwork displayed in Now Playing and playlists (fetched from Navidrome)
- Credentials saved in foobar2000's config (persistent across restarts)
- Test Connection button to verify server connectivity
- **Native `navidrome://` URI scheme**: tracks added to playlists store a stable URI, not a transient HTTP URL — playlists survive credential rotation or server URL changes
- Appears under **Preferences › Media Library › Library viewers** alongside Album List / Artist View, and (macOS) can also be docked as a panel in the main window layout via **Preferences › Display › Layout › Edit Layout**
- **Lyrics on Windows** via [ESLyric](https://github.com/ESLyric/release) — see [Lyrics (ESLyric)](#lyrics-eslyric-windows)

## Lyrics (ESLyric, Windows)

On Windows, foo_navidrome can feed lyrics into [ESLyric](https://github.com/ESLyric/release), a separate foobar2000 component that displays synced/plain lyrics panels. foo_navidrome doesn't render lyrics itself — it generates the config + lookup script ESLyric needs to talk to your Navidrome server.

1. Download the latest ESLyric release from **https://github.com/ESLyric/release** and install it the same way as foo_navidrome (drag the `.fb2k-component` onto foobar2000). Match the architecture to your foo_navidrome install (x64 / ARM64EC).
2. Open **Preferences › Tools › Navidrome** once and click **OK**/**Apply** — even with no changes, this is what makes foo_navidrome (re)generate the ESLyric config/script for your current server + credentials. It also runs automatically on every foobar2000 startup once ESLyric is detected, and again whenever you save Custom Headers.
3. Add the ESLyric panel to your layout (Default UI / Columns UI element picker).
4. Play a track that foo_navidrome added (a `navidrome://track/<id>` URI). ESLyric calls Navidrome's `getLyricsBySongId.view` for that song, falling back to the classic artist/title `getLyrics.view` lookup on servers that don't support by-id lookup. Synced lyrics are converted to LRC timing when the server provides them.

Generated files live under your foobar2000 profile:

```
%APPDATA%\foobar2000-v2\eslyric-data\scripts\lib\foo_navidrome\config.js   # server URL + Subsonic token — never your raw password
%APPDATA%\foobar2000-v2\eslyric-data\scripts\searcher\navidrome.js        # the lyrics-fetching script
```

Don't hand-edit them — they're overwritten on the next config save/startup. If ESLyric isn't installed, foo_navidrome silently skips this step (check **View › Console** for a one-line "not detected" note on startup).

## Platform Support

| Platform | Status |
|----------|--------|
| macOS         | ✅ Supported — shipped (Xcode, foobar2000 v2 for Mac) |
| Windows x64   | ✅ Supported — runtime-verified on native foobar2000 for Windows (Visual Studio 2022, or cross-compiled on Linux) |
| Windows x86   | ✅ Supported — same build, 32-bit binary shipped in the component |
| Windows ARM64 | ✅ Supported — native ARM64EC binary, runtime-verified |
| Linux         | ⚙️ Runs under Wine — loads the Windows x64 build |

> A single `.fb2k-component` ships every platform: macOS bundle, Windows x86, x64,
> and a native ARM64EC binary; foobar2000 selects the right one at load. The Windows
> binaries build natively with Visual Studio, on CI, or **cross-compiled on Linux**
> with clang-cl (see [Building on Linux](#building-on-linux-wine)).

---

## Building on macOS

### Prerequisites

- [foobar2000 v2 for Mac](https://www.foobar2000.org/mac)
- Xcode 14+
- foobar2000 SDK — source the SDK subdirs and `pfc/` from [reupen/foobar2000-sdk-unmodified](https://github.com/reupen/foobar2000-sdk-unmodified) (an unmodified mirror of the official SDK; crucially it ships `helpers-mac/`, which the macOS build needs and some other mirrors omit). Arrange them as siblings of this repo:

```
foobar2000/
  SDK/
  helpers/
  helpers-mac/                  ← macOS only (NSView+embed.m); from the reupen mirror
  shared/
  foobar2000_component_client/
  foo_navidrome/                ← this repo
pfc/                            ← sibling of foobar2000/ (also from the reupen mirror)
```

The expected layout relative to `foo_navidrome/`:

```
../SDK/
../helpers/
../shared/
../foobar2000_component_client/
../../pfc/
```

> If `pfc` is in a different location, create a symlink:
> ```bash
> ln -s /path/to/pfc /path/to/personal/pfc
> ```

### Build steps

The fastest dev loop is `./scripts/mac-dev-build.sh` — bumps the patch version in
`version.txt`, runs `xcodebuild`, and installs to your local foobar2000:

```bash
./scripts/mac-dev-build.sh   # bump patch + build + install (see Scripts for --minor/--no-bump/… flags)
```

Restart foobar2000 after the script finishes to pick up the new version.

If you prefer Xcode directly:

1. Open `foo_navidrome.xcworkspace` in Xcode
2. Select the **`foo_navidrome`** scheme (top-left scheme selector)
3. Build: **Product › Build** or `Cmd+B`
4. Run the install script:
   ```bash
   ./scripts/install-macos.sh
   ```
5. Restart foobar2000

### Configuration

1. Open **Preferences › Tools › Navidrome**
2. Enter your server URL (e.g. `http://navidrome.local:4533/`)
3. Enter your username and password
4. Click **Test Connection** — you should see "Connected!"

### Usage

Two ways to open the browser:
- **File › Open Navidrome Browser**
- **Preferences › Media Library › Library viewers › Navidrome › Activate**

Then:
- Expand an artist to see albums, expand an album to see songs
- Expand a smart list (★ Starred, Recently Added, Most Played, Recently Played, Random Albums), **Genres** or **Playlists** to browse without digging through artists
- Select one or more items and click **Add to Playlist** or **Play Now**
- Double-click a song to play it immediately
- Use the search field to search across your library
- Right-click for **Star / Unstar**, a **Rating** submenu (None, 1-5 stars), **Send Active Playlist to Navidrome** and **Download Original Files…**

Any of those actions work on whole albums or artists too — the component expands
the selection to tracks for you. Ratings apply to songs only, which is what
Subsonic supports.

#### Showing ratings in the playlist

The server-side rating and favorite flag travel with the track into foobar's
playlist, as the title-formatting fields `%navidrome_rating%` (1-5) and
`%navidrome_starred%` (`1` when starred). Add a column for them under
*Preferences › Display › Playlist View › Custom Playlist Columns*:

| Name | Pattern |
| --- | --- |
| Navidrome Rating | `$if(%navidrome_rating%,$repeat(★,%navidrome_rating%),)` |
| Navidrome Starred | `[%navidrome_starred%]` |

Both fields are absent for unrated / unstarred tracks, so `$if()` and
`[%navidrome_rating%]` behave as you would expect. They are deliberately not
called `%rating%` — that name belongs to foobar2000's own Playback Statistics,
which stores a separate local rating.

Playlist entries follow the server. The value in the playlist is refreshed in
place — same entry, no duplicate — whenever the component learns a newer one:

- **you rate or star a track** from the browser's right-click menu
- **you open the album** in the browser (or search, or expand a playlist or
  genre) — the browse response already carries the current values, so this costs
  no extra request
- **the track starts playing** — one lookup per played track. This is how a
  rating you changed in the Navidrome web UI reaches a playlist you never
  browsed.

- **foobar2000 starts** — a full refresh of every playlist, so a rating column
  can be sorted on rather than being right only for the tracks you happened to
  play. This is grouped by album: one request per distinct album across all
  playlists, not one per track. An album-shaped playlist costs a handful of
  requests; one spanning your whole library costs as many as it has albums, so
  it can be turned off under *Preferences › Advanced › Tools › Navidrome:
  refresh ratings on startup*.

**Upgrading:** tracks that were already in a playlist before this version don't
carry the album id the startup refresh groups by, so that one pass skips them
and says how many it skipped. They are not stuck — the other three triggers
still reach them, and opening their album in the browser updates them
immediately, no re-adding needed. Re-adding a track is only needed if you want
that entry to take part in the *startup* pass as well, since the album id is
written into the URI when the track is added.

#### Managing server playlists

The right-click menu edits the playlists stored on Navidrome, not just foobar's
local ones:

- **Add to Navidrome Playlist ▸** lists every playlist on the server; pick one to
  append the selection to it, or **New Playlist…** to create one from the
  selection.
- **Remove from Playlist** works on song rows sitting inside a **Playlists ›
  <name>** node — that's where a track has a position on the server to remove.
- **Rename Playlist…** / **Delete Playlist…** act on a selected playlist row.
  Deleting removes it for every client; the tracks themselves are untouched.

#### Streaming quality

*Preferences › Tools › Navidrome* has two settings that apply to every track:

- **Stream as** — `Server default` leaves it to Navidrome's own transcoding
  rules, `Original (no transcoding)` always sends the stored file, or pick a
  target format to have the server transcode on the fly: MP3, Opus, AAC,
  FLAC or WAV.
- **Max bitrate** — a kbps ceiling the server may not exceed. Ignored when the
  stream isn't being transcoded, and when the target is lossless.

> **The server has to be able to produce the format you pick.** Navidrome ships
> transcodings for MP3, Opus and AAC only. To use **FLAC** or **WAV**, add a
> matching transcoding under *Navidrome › Settings › Transcoding* first —
> otherwise the server ignores the request and sends the original file.
>
> Lossless targets aren't about saving bandwidth (WAV is bigger than almost any
> source): they're for playing formats foobar2000 can't decode itself, by having
> the server convert them without quality loss.

Both take effect on the next track; nothing needs restarting. **Download
Original Files…** ignores them by design — downloads are always the file as
stored on the server.

Plays are reported to the server as you listen (`scrobble.view`): "now playing"
when a track starts, then a play once half of it — or four minutes, whichever
comes first — has been heard. Turn it off with **Report plays to Navidrome
(scrobbling)** in *Preferences › Tools › Navidrome*.

Tracks land in the playlist as `navidrome://track/<id>?...` URIs. The component resolves these to the current HTTP stream when playback starts, so your playlists keep working if you change credentials or move the server.

---

## Building on Windows

### Prerequisites

- [foobar2000 v2 for Windows](https://www.foobar2000.org/)
- Visual Studio 2022 (with Desktop C++ workload)
- foobar2000 Windows SDK — same directory layout as above

### Build steps

1. Open `Windows/foo_navidrome.vcxproj` in Visual Studio
2. Update the `<ProjectReference>` GUIDs in the `.vcxproj` to match your local SDK project GUIDs
3. Build in **Release | x64** configuration (the `.vcxproj` also defines **Win32** and **ARM64EC** if you need those binaries; CI builds all three)
4. Copy `foo_navidrome.dll` to your foobar2000 components folder:
   ```
   %APPDATA%\foobar2000\user-components\foo_navidrome\
   ```
5. Restart foobar2000

> CI builds all three Windows arches (x86, x64, ARM64EC) and merges them with the
> macOS bundle into one `foo_navidrome_<version>.fb2k-component` per release.
> No Windows machine? **Cross-compile the x64 DLL on Linux** (see below), or run
> `./scripts/win-test.sh` to build on a GitHub Actions Windows runner (real
> MSVC/MSBuild) and download the artifact.

---

## Building on Linux (Wine)

foobar2000 has no native Linux build, but it runs well under **Wine** (it *is* the
Windows build). This repo can **cross-compile the Windows x64 component natively on
Linux** with `clang-cl` + `lld-link` — no MSVC and no Wine needed for the build
itself; Wine only runs foobar2000.

### Prerequisites

- foobar2000 for Windows installed under Wine (e.g. the AUR `foobar2000` package)
- `llvm`, `clang`, `lld` (provide `clang-cl` / `lld-link`), plus `git`, `curl`, `unzip`, `zip`

### One-time toolchain setup

```bash
./scripts/win-setup-toolchain.sh
```

Installs the LLVM toolchain, fetches the Windows SDK/CRT/**ATL** via
[`xwin`](https://github.com/Jake-Shadle/xwin), downloads [WTL](https://sourceforge.net/projects/wtl/),
and clones the foobar2000 SDK into the sibling layout (`../foobar2000`, `../pfc`, `../libPPUI`).

### Build + install loop

```bash
./scripts/win-build-local.sh   # cross-compile + install into the Wine foobar2000 (see Scripts for --launch/--clean)
```

The DLL is installed to `~/.foobar2000/profile/user-components-x64/foo_navidrome/`
and a distributable `foo_navidrome_<version>_win-x64.fb2k-component` is written to the
repo root. `scripts/install-windows.sh` handles the install + packaging and can be run
standalone after a build.

---

## Scripts

All helper scripts live in [`scripts/`](scripts/), grouped below by the OS you run
them on. Note the cross-platform twist: this project is currently developed on
**Linux**, and the **Windows component is built from Linux** (foobar2000 runs under
Wine; the DLL is cross-compiled with clang-cl) — so the "Windows" scripts run on a
Linux host. A *native* Windows build uses Visual Studio directly and needs no scripts.

A root [`Makefile`](Makefile) wraps the most common ones as `make` targets — run
`make help` for the full list. It's a thin convenience wrapper; the scripts below
remain the source of truth. Highlights:

Targets follow an `<os>-<action>` naming scheme — `win-*` for the Linux
cross-compile, `mac-*` for the native macOS build, `win-vm-*` for the
Windows-on-macOS VM flow.

```bash
make test          # Linux: fast clang-cl+wine build/run of MediaEnrichmentLogicTests
make win-build      # Linux: cross-compile the Windows x64 component
make win-build-launch  # …same, then relaunch Wine foobar2000
make win-logs       # follow the colourised component debug log (Wine)
make win-install     # Linux: install the built DLL + package the .fb2k-component
make win-test ARGS="--launch"   # dispatch a build-windows.yml run on GH, install, relaunch
make mac-build      # macOS: bump patch, xcodebuild Release, install
make mac-logs       # follow the colourised component debug log (macOS)
make mac-release    # macOS: bump, build, install, package, gh release create
make mac-ci-build VERSION=1.11.0     # hermetic macOS CI build
make win-vm-test ARGS="--launch" # Windows-on-macOS VM: cross-build, deploy, relaunch
```

### Linux — build & test the Windows component (current dev environment)

| Command | What it does |
|---------|--------------|
| `./scripts/win-setup-toolchain.sh` | One-time setup: install `clang-cl`/`lld-link`, fetch the Windows SDK/CRT/ATL (via xwin) + WTL, clone the SDK siblings |
| `./scripts/win-build-local.sh` | Cross-compile the x64 DLL and install it into the Wine foobar2000 |
| `./scripts/win-build-local.sh --launch` | …same, then relaunch foobar2000 |
| `./scripts/win-build-local.sh --clean` | Wipe the object cache and rebuild from scratch |
| `./scripts/navidrome-logs.sh` | Follow the local debug log, colourised by level/tag (run in a second pane; also `make win-logs` / `make mac-logs`) |
| `./scripts/install-windows.sh` | Install an already-built DLL + package the `.fb2k-component` (called by `win-build-local.sh`; `--new-release` to publish) |
| `./scripts/win-test.sh` | Build the DLL on a GitHub Actions Windows runner (real MSVC/MSBuild), download the artifact, install it locally |

Both local dev builds (`win-build-local.sh`, `mac-dev-build.sh` — but **not**
`--new-release` or CI) define `NAVIDROME_DEBUG_LOG`, so the component writes structured
traces (HTTP requests with auth values redacted, Subsonic status errors, input
decode steps, UI actions) to `/tmp/foo_navidrome_debug.log` (Wine writes it via
`Z:\tmp`). The build clears that file and marks it with the version; `make
win-logs` / `make mac-logs` (`scripts/navidrome-logs.sh`) tail it live. Release / CI
builds never define the flag — the tracing compiles to nothing.

### macOS — build & install the native Mac component

| Command | What it does |
|---------|--------------|
| `./scripts/mac-dev-build.sh` | Bump patch version, `xcodebuild` Release, install locally |
| `./scripts/mac-dev-build.sh --minor` · `--major` | Bump minor / major instead of patch |
| `./scripts/mac-dev-build.sh --no-bump` | Rebuild + install at the current version |
| `./scripts/mac-dev-build.sh --no-install` | Build only (skip install) |
| `./scripts/mac-dev-build.sh --new-release` | Build, install, package, then create a GitHub release (no debug tracing — matches CI) |
| `./scripts/install-macos.sh` | Install an already-built component + package the `.fb2k-component` (called by `mac-dev-build.sh`) |
| `./scripts/navidrome-logs.sh` | Follow `/tmp/foo_navidrome_debug.log`, colourised (also `make mac-logs`; needs a build from `mac-dev-build.sh` without `--new-release`) |

### macOS — build & test the *Windows* component in a Win11 ARM VM

No Windows machine needed: cross-compile the Windows x64 DLL on the Mac
(`clang-cl` + `lld-link` + `xwin`) and runtime-test it in a headless Windows 11
ARM64 QEMU/HVF guest. See [`scripts/win-vm/README.md`](scripts/win-vm/README.md)
for the full walkthrough.

| Command | What it does |
|---------|--------------|
| `./scripts/win-vm/setup-mac-toolchain.sh` | One-time: Homebrew deps + xwin CRT/SDK/ATL + WTL + foobar SDK |
| `./scripts/win-vm/fetch-win11-arm.sh` | Build the Win11 ARM ISO (uupdump) + fetch the virtio ISO |
| `./scripts/win-vm/win-vm.sh install` | Unattended headless Windows install into the QEMU guest |
| `./scripts/win-vm/win-vm-test.sh --launch` | Cross-build the x64 DLL → deploy into the guest over SSH → relaunch foobar2000 |

### CI (GitHub Actions — not run by hand)

| Script | What it does |
|--------|--------------|
| `scripts/mac-ci-build.sh <version>` | macOS Release build + packaging, invoked by semantic-release during a release |

The Windows CI build is driven by `.github/workflows/build-windows.yml` (MSBuild on a
`windows-latest` runner) — no shell script involved.

---

## Project Structure

```
foo_navidrome/
├── main.cpp                        # Shared: component version + filename
├── stdafx.h                        # Shared precompiled header
├── SubsonicTypes.h                 # Shared pure-C++ data types
├── SubsonicClient.h/.mm            # macOS: ObjC Subsonic HTTP client
├── NavidromeInput.h/.mm            # macOS: input_singletrack handler for navidrome:// URIs
├── NavidromePlugin.mm              # macOS: plugin registration, cfg vars, prefs, menu, library_viewer
├── NavidromeArtExtractor.mm        # macOS: album art fallback (album_art_fallback)
├── Mac/
│   ├── NavidromeBrowserController.h/.mm    # macOS: NSWindowController browser UI
│   └── NavidromePreferencesController.h/.mm # macOS: NSViewController preferences
├── Windows/
│   ├── stdafx.h/.cpp               # Windows precompiled header
│   ├── SubsonicClientWin.h/.cpp    # Windows: WinHTTP Subsonic client
│   ├── NavidromePluginWin.cpp      # Windows: plugin registration, cfg vars, prefs, menu, art
│   ├── NavidromeInputWin.h/.cpp    # Windows: navidrome:// input_singletrack handler
│   ├── BrowserWindow.h/.cpp        # Windows: ATL browser window
│   ├── MediaEnrichmentLogic.h/.cpp # Windows: URI/cover-art/ESLyric-config logic (SDK-free, unit-tested)
│   ├── EsLyricBridge.h/.cpp        # Windows: writes the ESLyric config + searcher script
│   ├── EsLyricScript.h             # Windows: embedded ESLyric searcher script source
│   ├── tests/                      # Windows: standalone unit tests for MediaEnrichmentLogic
│   └── foo_navidrome.vcxproj       # Visual Studio project
├── scripts/                        # build / install / toolchain helpers
│   ├── mac-dev-build.sh            #   macOS dev loop (bump + xcodebuild + install)
│   ├── mac-ci-build.sh             #   macOS CI build (called by semantic-release)
│   ├── install-macos.sh            #   macOS install + package helper
│   ├── win-setup-toolchain.sh      #   Linux: provision clang-cl + xwin SDK/ATL + WTL
│   ├── win-build-local.sh          #   Linux: cross-compile the Windows DLL + install
│   ├── navidrome-logs.sh           #   follow the colourised component debug log (win + mac)
│   ├── install-windows.sh          #   Windows install + package helper
│   └── win-test.sh                 #   build the Windows DLL on CI + install locally
├── foo_navidrome.xcodeproj/        # Xcode project
└── foo_navidrome.xcworkspace/      # Xcode workspace (includes SDK projects)
```

## Contributing

Pull requests are welcome. This section is the fast path from a fresh clone to a merged change.

### Getting set up

1. Fork and clone the repo, then lay out the sibling SDK directories described under [Prerequisites](#prerequisites) — the project will not build without `pfc/` and `foobar2000/{SDK,helpers,helpers-mac,shared,foobar2000_component_client}` next to it. The CI workflow clones [reupen/foobar2000-sdk-unmodified](https://github.com/reupen/foobar2000-sdk-unmodified) into exactly that layout if you want a reference.
2. Pick your platform's build path: [macOS](#building-on-macos) (Xcode), [Windows](#building-on-windows) (Visual Studio 2022), or [Linux → Windows cross-compile](#building-on-linux-wine) (clang-cl + Wine, the current primary dev environment).
3. Read [`CLAUDE.md`](CLAUDE.md) before touching code — it documents the architecture, the per-platform file map, the design decisions, and a long list of hard-won gotchas (URI parsing traps, dual-mount `NSViewController` rules, CI log handling, etc.). It will save you hours.

### How the code is laid out

The component is a **shared C++ core + per-platform UI/HTTP layers** — see [Project Structure](#project-structure) for the full file map. Quick orientation:

- **Cross-platform / pure C++:** `main.cpp`, `stdafx.h`, `SubsonicTypes.h`.
- **macOS (ObjC++):** `SubsonicClient.mm` (NSURLSession HTTP), `NavidromePlugin.mm` (service registration), `NavidromeInput.mm` (the `navidrome://` input handler), `NavidromeArtExtractor.mm` (album art), `Mac/*` (the AppKit browser + preferences UI).
- **Windows (Win32/ATL):** `Windows/SubsonicClientWin.cpp` (WinHTTP HTTP), `Windows/NavidromePluginWin.cpp`, `Windows/BrowserWindow.*`.

If you fix a bug in the data layer, check whether the **macOS and Windows HTTP clients both need it** — they are separate implementations of the same Subsonic protocol.

### Key architectural concepts to know

- **`navidrome://track/<id>?...` URI scheme.** Tracks are queued as these URIs, not raw HTTP URLs, so playlists survive credential/server changes. Metadata is embedded in the query string; the stream URL is resolved at decode time. **Any code that parses these URIs must be updated together when the scheme changes** — `NavidromeInput`, the art extractor's `is_our_path`, etc. The host-vs-path RFC-3986 trap is documented in `CLAUDE.md`.
- **GUIDs.** Every registered service has a hardcoded `static constexpr GUID`. **If you fork this component you must regenerate all of them** (`NavidromePlugin.mm` and `NavidromeInput.mm`) — two components sharing a GUID will collide in foobar2000.
- **Logging is the debugger.** There's no practical debugger-attach for foobar2000 components on Mac; use the `navi_log` file-logging helper (writes to `/tmp/foo_navidrome.log`, survives a crash). Remove temporary logs before submitting.

### Coding conventions

- Match the surrounding style (naming, comment density, indentation) rather than introducing a new one.
- Keep platform-specific code behind the existing macOS / `Windows/` split — don't `#ifdef` platform branches into the shared core unless there's no alternative.
- Update `CLAUDE.md` when you make a decision or hit a gotcha worth recording for the next contributor.

### Commits, versioning, and PRs

- Commit messages **must** follow [Conventional Commits](https://www.conventionalcommits.org/) — the release pipeline parses them to decide the version bump. See [Commit message convention](#commit-message-convention). In short: `feat:` → minor, `fix:`/`perf:`/`refactor:` → patch, `chore:`/`docs:`/`ci:`/etc. → no release, `!` or a `BREAKING CHANGE:` footer → major.
- **Don't bump `version.txt` or edit `CHANGELOG.md` by hand** — semantic-release owns both. Your job is just well-typed commits.
- Test on at least one platform and say which one in the PR. Cross-platform changes ideally get verified on both macOS and a Windows build (native, or cross-compiled + run under Wine — see the Linux section).
- Keep PRs focused; one logical change per PR makes review and the auto-generated changelog far cleaner.

### Good places to start

- Offline / caching support for streamed tracks.
- A Windows equivalent of the macOS `ui_element_mac` layout panel (dock the browser inside the main window layout, not just the standalone window / prefs page).

## Releasing

Releases are automated. Every push to `main` triggers `.github/workflows/release.yml`, which:

1. Lays out the sibling-directory tree the project expects (`pfc/`, `foobar2000/{SDK,helpers,helpers-mac,shared,foobar2000_component_client}`) by cloning [reupen/foobar2000-sdk-unmodified](https://github.com/reupen/foobar2000-sdk-unmodified) — an unmodified mirror of the official SDK that includes `helpers-mac/`.
2. Runs [`semantic-release`](https://semantic-release.gitbook.io/) per `.releaserc.json`. semantic-release inspects commits since the last tag and decides whether a release is needed.
3. If a release is needed:
   - `scripts/mac-ci-build.sh <next-version>` pins `version.txt`, runs `xcodebuild -configuration Release`, ad-hoc signs, and packages the macOS `foo_navidrome_<version>.fb2k-component`.
   - `CHANGELOG.md` is updated.
   - A `chore(release): <version> [skip ci]` commit lands on `main` with `version.txt` + `CHANGELOG.md`.
   - A GitHub release is created with the macOS `.fb2k-component` attached.
   - The reusable `build-windows.yml` workflow then builds the Windows x64 DLL (MSVC/MSBuild on a `windows-latest` runner) and attaches `foo_navidrome_<version>_win-x64.fb2k-component` to the same release.

### Commit message convention

The pipeline reads [Conventional Commits](https://www.conventionalcommits.org/). The commit type determines whether (and how) the version is bumped:

| Type                  | Effect          |
| --------------------- | --------------- |
| `feat: …`             | minor bump      |
| `fix: …` / `perf: …`  | patch bump      |
| `refactor: …`         | patch bump      |
| `chore: …` / `docs: …` / `style: …` / `test: …` / `ci: …` | no release |
| `feat!: …` or footer `BREAKING CHANGE:` | major bump |

Versions are tracked in `version.txt` (consumed by the Xcode "Generate Version Header" build phase, which writes the gitignored `version_generated.h`).

### Manual release (fallback)

```bash
# Build, install locally, package, and create a GitHub release in one shot
./scripts/mac-dev-build.sh --new-release
```

This bypasses the workflow and uses the `--new-release` path of `scripts/install-macos.sh`.

## License

This component's own source code is licensed under the **MIT License** — see the [LICENSE](LICENSE) file.

It builds against the **foobar2000 SDK** and **PFC**, which are *not* covered by this license — they are distributed under their own terms by the foobar2000 author and are not included in this repository (the build fetches them separately; see the build instructions above). Redistribution of the SDK is subject to those terms.
