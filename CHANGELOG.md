## [1.23.0](https://github.com/santiagorod92/foo_navidrome/compare/v1.22.0...v1.23.0) (2026-10-09)


### Features

* **logging:** release builds now keep a log, `foo_navidrome.log`, in the foobar2000 profile. It records warnings and errors, rotates past 2 MB, and records full detail while *Preferences › Advanced › Tools › Navidrome: verbose logging* is on (applies immediately, no restart). Each session opens with a line giving the component, foobar2000, OS, architecture and Wine versions.
* **diagnostics:** a **Copy Diagnostics** button (Preferences › Tools › Navidrome, both platforms) copies a report for bug reports to the clipboard. It includes versions, the server type, version and OpenSubsonic extensions, the relevant settings and the recent log. Credentials and custom header values are never included, and the server address is replaced by `<server>`. The log file is reachable with **Open Log Folder** (Windows) or **Show Log in Finder** (macOS).
* **server capabilities:** the component reads the server's OpenSubsonic extension list once per session and skips endpoints the server doesn't support. Lyrics on a server without `songLyrics` go straight to the artist/title lookup instead of first waiting for a failed request.
* **browser:** a browse list that has gone stale reloads itself. Reopening a browser window closed for more than 30 minutes reloads it from the server, and an Add/Play that resolves to nothing because the server rescanned since the list loaded reloads the list and asks you to select again.
* **browser:** when Add to Playlist / Play Now can't load some or all of the tracks, an error window explains what failed, quotes the server's error and suggests **Refresh**.
* **macOS:** **Remove Bookmark** in the browser's right-click menu (it was Windows-only).
* **preferences:** the main Navidrome page is grouped under *Navidrome Server Connection*, *Rescan Navidrome Library* and *Logs and Troubleshooting* headings on both platforms. The rescan button is now labelled **Rescan**, and the macOS page no longer carries the "After saving, open File › Open Navidrome Browser" hint.
* **bug reports:** GitHub issue forms for bug reports and feature requests. The bug form walks through verbose logging → reproduce → Copy Diagnostics.


### Bug Fixes

* failures of bookmark removal and of creating, editing or deleting a radio station (browser menu and Radio Stations preferences page) are now written to the log on both platforms instead of only flashing in the status line.


### Code Refactoring

* `SubsonicTypes.h` split into topic headers (`SubsonicModels.h`, `SubsonicErrors.h`, `TrackUri.h`, `LibraryFilter.h`, `Json.h`, `SubsonicParsers.h`) behind the same umbrella include.
* Windows browser split: `BrowserWindow.cpp` keeps the window and tree, server actions move to `BrowserWindowActions.cpp`, the modal prompts to `BrowserPrompts.cpp`, and the Radio Stations and Libraries preference pages to their own files.
* macOS browser split: server actions move into a `NavidromeBrowserController (Actions)` category with a shared private header.
* Comments removed from the code (`src/`, `tests/`, `scripts/`, `tools/`, `Makefile`, workflows): about 3,700 fewer lines with no change in behaviour. Each script's `--help` now prints a built-in usage text.


### Build System

* the local Windows build (`win-build-local.sh`) tracks header dependencies and compile flags, so a header edit or a `--release-log` switch rebuilds exactly what it affects. Previously, a header edit without `--clean` could produce a DLL that crashed at runtime. A no-change build takes about 1.5 s.

## [1.22.0](https://github.com/santiagorod92/foo_navidrome/compare/v1.21.2...v1.22.0) (2026-10-09)


### Features

* Navidrome Browser as a Default UI panel on Windows ([#24](https://github.com/santiagorod92/foo_navidrome/issues/24)) ([14f9586](https://github.com/santiagorod92/foo_navidrome/commit/14f958612b32c25d5034acded710787079154f4d))


### Bug Fixes

* drop libPPUI WIN32_OP from the Default UI element so the MSBuild link succeeds ([a0b1466](https://github.com/santiagorod92/foo_navidrome/commit/a0b1466258514af5abea3c13336c9c374bc9656f))

## [1.21.2](https://github.com/santiagorod92/foo_navidrome/compare/v1.21.1...v1.21.2) (2026-10-08)


### Bug Fixes

* warning-free release builds on macOS and Windows ([#21](https://github.com/santiagorod92/foo_navidrome/issues/21)) ([2a46400](https://github.com/santiagorod92/foo_navidrome/commit/2a4640036a9599e4c9d79764dc2ad9bd70f8e8ee))

## [1.21.1](https://github.com/santiagorod92/foo_navidrome/compare/v1.21.0...v1.21.1) (2026-10-08)


### Bug Fixes

* Windows prefs dark mode, high-DPI layout and garbled text ([#18](https://github.com/santiagorod92/foo_navidrome/issues/18)) ([#20](https://github.com/santiagorod92/foo_navidrome/issues/20)) ([259ba3f](https://github.com/santiagorod92/foo_navidrome/commit/259ba3f56be6a86ac7e7667c7c640f08996ddb3b))

## [1.21.0](https://github.com/santiagorod92/foo_navidrome/compare/v1.20.0...v1.21.0) (2026-10-07)


### Features

* Instant Mix and AudioMuse-AI integration ([#16](https://github.com/santiagorod92/foo_navidrome/issues/16)) ([#19](https://github.com/santiagorod92/foo_navidrome/issues/19)) ([f91b545](https://github.com/santiagorod92/foo_navidrome/commit/f91b545f660e4d38515dd24f8e8e38a9213e0eaa))

## [1.20.0](https://github.com/santiagorod92/foo_navidrome/compare/v1.19.0...v1.20.0) (2026-10-07)


### Features

* lyrics on macOS — Navidrome Lyrics panel and navidrome_lyrics_api ([#17](https://github.com/santiagorod92/foo_navidrome/issues/17)) ([ac90536](https://github.com/santiagorod92/foo_navidrome/commit/ac90536ff8a144720d7161422950a1c0f92cefae))

## [1.19.0](https://github.com/santiagorod92/foo_navidrome/compare/v1.18.0...v1.19.0) (2026-10-02)


### Features

* navidrome_library_api on macOS ([#15](https://github.com/santiagorod92/foo_navidrome/issues/15)) ([fea1636](https://github.com/santiagorod92/foo_navidrome/commit/fea16365ff257e5ebdb6aaeca646d38548491bf7))

## [1.18.0](https://github.com/santiagorod92/foo_navidrome/compare/v1.17.2...v1.18.0) (2026-10-01)


### Features

* All Songs node and multi-select in the browser ([7c3a0c5](https://github.com/santiagorod92/foo_navidrome/commit/7c3a0c55e4d634ca74795b7ec4ab4f57436f7546))

## [1.17.2](https://github.com/santiagorod92/foo_navidrome/compare/v1.17.1...v1.17.2) (2026-09-30)


### Code Refactoring

* split sources into src/core and src/platform/{mac,win} ([7c2bb60](https://github.com/santiagorod92/foo_navidrome/commit/7c2bb605e3200e32d208dcb5346629a566de051d))

## [1.17.1](https://github.com/santiagorod92/foo_navidrome/compare/v1.17.0...v1.17.1) (2026-09-30)


### Bug Fixes

* **build:** require macOS 12 and force the deployment target ([2e6924e](https://github.com/santiagorod92/foo_navidrome/commit/2e6924ee215fa124d376c7d7bff617fae1e63243))
* **mac:** stop the browser looping forever on Similar Artists ([51ff08a](https://github.com/santiagorod92/foo_navidrome/commit/51ff08ae3df75a56be636a922aedc68907e6025c))

## [1.17.0](https://github.com/santiagorod92/foo_navidrome/compare/v1.16.0...v1.17.0) (2026-09-29)


### Features

* auto-skip tracks deleted from the server via fb2k::skipTrack ([adfaf6a](https://github.com/santiagorod92/foo_navidrome/commit/adfaf6a6c66d0d486771eb07880881009ff5425c))
* expose navidrome_library_api for cross-component library browsing ([81732dc](https://github.com/santiagorod92/foo_navidrome/commit/81732dcf668e24820b490eef3ee3f4134a98d30f))


### Bug Fixes

* **build:** predefine _NO_SYS_GUID_OPERATOR_EQ_ in the clang-cl forced-include prefix ([14a7523](https://github.com/santiagorod92/foo_navidrome/commit/14a7523326361cbc82e600498f268c82c3a268c3))

## [1.16.0](https://github.com/santiagorod92/foo_navidrome/compare/v1.15.4...v1.16.0) (2026-09-25)


### Features

* expose navidrome_rating_api for cross-component rating writes ([2437422](https://github.com/santiagorod92/foo_navidrome/commit/2437422ed5c7f13980fa275f7b5a0a558cbda91d))

## [1.15.4](https://github.com/santiagorod92/foo_navidrome/compare/v1.15.3...v1.15.4) (2026-09-22)


### Bug Fixes

* drop u8 prefix from test literals (C++20 char8_t breaking change) ([9956227](https://github.com/santiagorod92/foo_navidrome/commit/995622740f11c068124ceb8d30147db728ad5e73))

## [1.15.3](https://github.com/santiagorod92/foo_navidrome/compare/v1.15.2...v1.15.3) (2026-09-22)


### Bug Fixes

* use printf not echo to patch stdafx.cpp (missing trailing newline) ([d4d3151](https://github.com/santiagorod92/foo_navidrome/commit/d4d31519e58f0cb1a8753f7a8053397e6c915886))

## [1.15.2](https://github.com/santiagorod92/foo_navidrome/compare/v1.15.1...v1.15.2) (2026-09-22)


### Bug Fixes

* work around SDK's missing wrap_pfc_hooks.h include for ARM64EC ([be78524](https://github.com/santiagorod92/foo_navidrome/commit/be78524d419964ec8bfa11c717d24c361d9dac0c))

## [1.15.1](https://github.com/santiagorod92/foo_navidrome/compare/v1.15.0...v1.15.1) (2026-09-22)


### Bug Fixes

* bump to C++20 for the same upstream SDK sync that renamed pfc ([564499c](https://github.com/santiagorod92/foo_navidrome/commit/564499c0fcdaa73ba807313a6ada96734e67a98f))

## [1.15.0](https://github.com/santiagorod92/foo_navidrome/compare/v1.14.0...v1.15.0) (2026-09-22)


### Features

* add Artist Info and Top Songs/Similar Artists browsing ([8e728c1](https://github.com/santiagorod92/foo_navidrome/commit/8e728c151ae75140e3c122370d8956fda807f2d5))


### Bug Fixes

* link against pfc's renamed product (libpfc.a, not libpfc-Mac.a) ([d619942](https://github.com/santiagorod92/foo_navidrome/commit/d619942c673074f8e7c62afd51487ff7f81b423b))
* pin an explicit Xcode scheme so the macOS release build stops relying on autocreation ([71d3437](https://github.com/santiagorod92/foo_navidrome/commit/71d3437c0c93c2700b6569affb2f35732ed1e68f))

## [1.14.0](https://github.com/santiagorod92/foo_navidrome/compare/v1.13.5...v1.14.0) (2026-09-16)


### Features

* add Podcast and Now Playing browsing ([767886c](https://github.com/santiagorod92/foo_navidrome/commit/767886cb1416b1d4870124797db18fc768a665d8))


### Bug Fixes

* log context-menu failures for star/rate/playlist operations ([8111410](https://github.com/santiagorod92/foo_navidrome/commit/811141099795a6527f6a8fec6aa17be94cc3404a))

## [1.13.5](https://github.com/santiagorod92/foo_navidrome/compare/v1.13.4...v1.13.5) (2026-09-15)


### Code Refactoring

* consolidate remaining cross-platform browser/client duplication ([7fe58e8](https://github.com/santiagorod92/foo_navidrome/commit/7fe58e8620ab0a41a62f2b5a4db732bd61d1461c))

## [1.13.4](https://github.com/santiagorod92/foo_navidrome/compare/v1.13.3...v1.13.4) (2026-09-11)


### Code Refactoring

* share the Subsonic client core across Windows and macOS ([#13](https://github.com/santiagorod92/foo_navidrome/issues/13)) ([4b72bbc](https://github.com/santiagorod92/foo_navidrome/commit/4b72bbcbf6eb24a000a37650467ad7372ee05dfe))

## [1.13.3](https://github.com/santiagorod92/foo_navidrome/compare/v1.13.2...v1.13.3) (2026-09-08)


### Code Refactoring

* share the Subsonic client core across Windows and macOS ([8a79d84](https://github.com/santiagorod92/foo_navidrome/commit/8a79d842698ab81c8058a7b0b6b1f9ea8fb01bbf))

## [1.13.2](https://github.com/santiagorod92/foo_navidrome/compare/v1.13.1...v1.13.2) (2026-09-08)


### Code Refactoring

* share the browser tree logic across Windows and macOS ([#12](https://github.com/santiagorod92/foo_navidrome/issues/12)) ([6932aa3](https://github.com/santiagorod92/foo_navidrome/commit/6932aa3618fb569b2482f8ecf94a0dac3109d949))

## [1.13.1](https://github.com/santiagorod92/foo_navidrome/compare/v1.13.0...v1.13.1) (2026-09-07)


### Bug Fixes

* scope artist albums by library and group the browser tree by library ([#11](https://github.com/santiagorod92/foo_navidrome/issues/11)) ([d91e0e4](https://github.com/santiagorod92/foo_navidrome/commit/d91e0e47f863e634b38bc0f92b9453c7fef8719e)), closes [#9](https://github.com/santiagorod92/foo_navidrome/issues/9)

## [1.13.0](https://github.com/santiagorod92/foo_navidrome/compare/v1.12.2...v1.13.0) (2026-09-06)


### Features

* filter browsing by Navidrome library (multi-library support) ([#10](https://github.com/santiagorod92/foo_navidrome/issues/10)) ([b3548ce](https://github.com/santiagorod92/foo_navidrome/commit/b3548ce92f0c38eef39536dc3dbb9e7a88677771)), closes [#9](https://github.com/santiagorod92/foo_navidrome/issues/9)

## [1.12.2](https://github.com/santiagorod92/foo_navidrome/compare/v1.12.1...v1.12.2) (2026-09-06)

## [1.12.1](https://github.com/santiagorod92/foo_navidrome/compare/v1.12.0...v1.12.1) (2026-09-02)


### Bug Fixes

* general logging improvements ([1bf7285](https://github.com/santiagorod92/foo_navidrome/commit/1bf72850d1958c0759a39c7046d179d83e0dd62e))

## [1.12.0](https://github.com/santiagorod92/foo_navidrome/compare/v1.11.0...v1.12.0) (2026-09-02)


### Features

* add Play Similar and Random Mix actions (macOS + Windows) ([949b641](https://github.com/santiagorod92/foo_navidrome/commit/949b641a9035a53c1adfe572bb5ee3ebcd61f62e))
* **debug:** cross-platform NAVIDROME_DEBUG_LOG tracer with make win-logs / mac-logs ([ca6bf80](https://github.com/santiagorod92/foo_navidrome/commit/ca6bf8009968d32d54f6e255186ff105e44bd104))

## [1.11.0](https://github.com/santiagorod92/foo_navidrome/compare/v1.10.0...v1.11.0) (2026-08-26)


### Features

* **scan:** add Rescan Library Now button to preferences ([2a932de](https://github.com/santiagorod92/foo_navidrome/commit/2a932decd3f2dfd1a0877835dd60603d92824134))


### Bug Fixes

* **search:** stop live search from piling up stale results ([30c0f16](https://github.com/santiagorod92/foo_navidrome/commit/30c0f16c530f870cea1a97d6dc7e8d9c282b6e55))

## [1.10.0](https://github.com/santiagorod92/foo_navidrome/compare/v1.9.0...v1.10.0) (2026-08-25)


### Features

* expose the Navidrome rating as a playlist tag and keep it current ([2f502a0](https://github.com/santiagorod92/foo_navidrome/commit/2f502a0f2c78387a82544f5b14e187b85c1bc1b3))
* rate and star from the playlist context menu ([5fc3369](https://github.com/santiagorod92/foo_navidrome/commit/5fc33692a3f518a88f6176444f0c9f7604e8b7dd))

## [1.9.0](https://github.com/santiagorod92/foo_navidrome/compare/v1.8.0...v1.9.0) (2026-08-25)


### Features

* add internet radio station browsing, playback, and CRUD ([2326783](https://github.com/santiagorod92/foo_navidrome/commit/2326783fa77ae189438824a955bdcd509c98af9b))

## [1.8.0](https://github.com/santiagorod92/foo_navidrome/compare/v1.7.0...v1.8.0) (2026-08-24)


### Features

* add ui_element_mac layout panel for macOS browser ([d30fa9b](https://github.com/santiagorod92/foo_navidrome/commit/d30fa9b47189e656294799690715e6296f696258))

## [1.7.0](https://github.com/santiagorod92/foo_navidrome/compare/v1.6.2...v1.7.0) (2026-08-24)


### Features

* add bookmarks / resume playback support ([d647547](https://github.com/santiagorod92/foo_navidrome/commit/d64754714b4f79ebdd4e0f4117afdc54c2e85e00))


### Bug Fixes

* add missing [@implementation](https://github.com/implementation) for SubsonicBookmark ([0d37e00](https://github.com/santiagorod92/foo_navidrome/commit/0d37e00017f05dbd4362bdb9173165019af3cbdb)), closes [#6](https://github.com/santiagorod92/foo_navidrome/issues/6)

## [1.6.2](https://github.com/santiagorod92/foo_navidrome/compare/v1.6.1...v1.6.2) (2026-08-23)


### Bug Fixes

* sync browser colors with foobar's Colours and Fonts scheme ([027c75e](https://github.com/santiagorod92/foo_navidrome/commit/027c75e83a1aef585d4e2ebd066ee07355dd7138)), closes [#4](https://github.com/santiagorod92/foo_navidrome/issues/4)

## [1.6.1](https://github.com/santiagorod92/foo_navidrome/compare/v1.6.0...v1.6.1) (2026-08-14)


### Bug Fixes

* enter action to clear current playlist ([22e91f1](https://github.com/santiagorod92/foo_navidrome/commit/22e91f1152fdd7f9dd38db172253674260ef405c))

## [1.6.0](https://github.com/santiagorod92/foo_navidrome/compare/v1.5.0...v1.6.0) (2026-08-14)


### Features

* browse the library by genre ([63cb4f7](https://github.com/santiagorod92/foo_navidrome/commit/63cb4f7fc171253addf461c5ef2e8c536f280841))
* download original files from the browser ([b879a30](https://github.com/santiagorod92/foo_navidrome/commit/b879a30962f1893faa27507f2690c406b2159f94))
* manage server playlists from the browser ([8224da6](https://github.com/santiagorod92/foo_navidrome/commit/8224da63861efb84d64215b5c82dcaaf02699d18))
* pick the streaming format and bitrate ([2c44673](https://github.com/santiagorod92/foo_navidrome/commit/2c446732acdd45e6c1f7a8bedb6f3379c4dda3da))


### Bug Fixes

* carry the track's codec suffix in navidrome:// URIs on macOS ([6d2065c](https://github.com/santiagorod92/foo_navidrome/commit/6d2065c30e7ac9113b0e27ec8787c6533900e3e9))

## [1.5.0](https://github.com/santiagorod92/foo_navidrome/compare/v1.4.1...v1.5.0) (2026-08-13)


### Features

* add scrobbling, smart lists, favorites/ratings and server playlists ([c52446c](https://github.com/santiagorod92/foo_navidrome/commit/c52446ca5463d852a41bc5ef7deb3dd87e7d3638))


### Bug Fixes

* **build:** resolve the SDK tree in dev-build.sh and stop hiding xcodebuild errors ([11db278](https://github.com/santiagorod92/foo_navidrome/commit/11db27852922f9f0dfa0d33ff9f82eb527a878e1))

## [1.4.1](https://github.com/santiagorod92/foo_navidrome/compare/v1.4.0...v1.4.1) (2026-08-12)


### Bug Fixes

* publish releases to foobar2000.org from self-hosted runner ([ece28b7](https://github.com/santiagorod92/foo_navidrome/commit/ece28b7d34365489997d51663fa01561746ef0c1))

## [1.4.0](https://github.com/santiagorod92/foo_navidrome/compare/v1.3.0...v1.4.0) (2026-08-12)


### Features

* add support to ESLync for Windows ([ef5c7aa](https://github.com/santiagorod92/foo_navidrome/commit/ef5c7aa3011c47084cfab7c243f58b16c99927c8))

## [1.3.0](https://github.com/santiagorod92/foo_navidrome/compare/v1.2.1...v1.3.0) (2026-07-21)


### Features

* right-click context menu and embedded Windows browser ([58305b7](https://github.com/santiagorod92/foo_navidrome/commit/58305b71dbd74a1579dca9d54782faf296215c4a))

## [1.2.1](https://github.com/santiagorod92/foo_navidrome/compare/v1.2.0...v1.2.1) (2026-06-18)


### Bug Fixes

* **windows:** make the Windows release build green ([9e57c75](https://github.com/santiagorod92/foo_navidrome/commit/9e57c75fcdf36faf544c5156cf2f6f9adfacd03b))

## [1.2.0](https://github.com/santiagorod92/foo_navidrome/compare/v1.1.0...v1.2.0) (2026-06-18)


### Features

* add an Enter shortcut to queue, play, and close the browser ([a972e89](https://github.com/santiagorod92/foo_navidrome/commit/a972e89a19dbd5da0d13f2f309e1496018db2618))
* send custom HTTP headers (Cloudflare Access) on every request ([dd6b6c7](https://github.com/santiagorod92/foo_navidrome/commit/dd6b6c7aec414eb9ce5109c153c54a895dbfd647))
* start playback honoring the Playback > Order setting ([4564f65](https://github.com/santiagorod92/foo_navidrome/commit/4564f65cb7c620feb0e6fb70b898516655b3960e))
* **windows:** add a Navidrome page under Preferences > Media Library ([178b767](https://github.com/santiagorod92/foo_navidrome/commit/178b767ba91936e5a8d28cc279dbee2c7304db53))
* **windows:** show album and year in the browser playlist ([3638a9a](https://github.com/santiagorod92/foo_navidrome/commit/3638a9a3aabf68038b17a2f577a42ac4c5e7af53))

## [1.1.0](https://github.com/santiagorod92/foo_navidrome/compare/v1.0.1...v1.1.0) (2026-05-23)


### Features

* general project improvements ([a6c2d89](https://github.com/santiagorod92/foo_navidrome/commit/a6c2d8918f54531f388a27b65453d1c68854f9db))


### Bug Fixes

* buid step ([7572610](https://github.com/santiagorod92/foo_navidrome/commit/75726100ed43f7d339b60b69f49c0d2b32c74dfb))
* build step again ([cd4bd8d](https://github.com/santiagorod92/foo_navidrome/commit/cd4bd8dfb939abfdc2770e0fd8b8dc6bf5f7ebd3))
* ci build ([8243e9b](https://github.com/santiagorod92/foo_navidrome/commit/8243e9b4fa15539f23972177e1aac9b72e4f6496))
* release action ([38a870c](https://github.com/santiagorod92/foo_navidrome/commit/38a870c090ab902b98b6df269f328b876aeaec8d))
* release action issues ([e6fe420](https://github.com/santiagorod92/foo_navidrome/commit/e6fe42012d10852c35f2434983448155cf5915c7))

# Changelog

All notable changes to this project are documented here. The file is regenerated
on every release by [semantic-release](https://semantic-release.gitbook.io/) from
[Conventional Commits](https://www.conventionalcommits.org/) in the git log.

Releases before automation:

## 1.0.10 — 2026-05-23

- Browser refactored from `NSWindowController` to `NSViewController`; mounted
  inline inside *Preferences › Media Library › Navidrome* (no extra window) and
  also wrapped in a standalone window from the File menu.

## 1.0.9 — 2026-05-23

- Cover art works for `navidrome://` URIs (the art extractor matched only the
  legacy `/rest/stream.view` HTTP URLs before).

## 1.0.8 — 2026-05-23

- Fixed `parse_uri` so song IDs are extracted correctly from
  `navidrome://track/<id>` URIs (RFC-3986 puts `track` in the authority, not the
  path).

## 1.0.5 — 2026-05-23

- Browser sub-page appears under *Preferences › Media Library › Navidrome*.

## 1.0.4 — 2026-05-23

- `install-macos.sh` prefers the Release build over a stale Debug build in
  DerivedData.

## 1.0.3 — 2026-05-23

- Single-source-of-truth versioning via `version.txt` + `dev-build.sh`.

## 1.0.1 — 2026-04-10

- Initial macOS release.
