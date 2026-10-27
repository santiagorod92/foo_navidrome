## [1.36.0](https://github.com/santiagorod92/foo_navidrome/compare/v1.35.0...v1.36.0) (2026-10-27)


### Features

* **browser:** **Artist Info** now finds a biography for artists stored as *Last, First* or with the article at the end (*Jackson, Michael*, *Beatles, The*, *Rolling Stones, The*) ([#49](https://github.com/santiagorod92/foo_navidrome/issues/49)). Navidrome asks last.fm by the exact artist name, so for these names it had nothing and the window read "No biography available.". When the server has no biography, foo_navidrome now asks last.fm itself: first with the stored name and last.fm's own name correction (the same one that redirects last.fm/music/Jackson,+Michael to Michael Jackson's page), then turned around into *First Last* (*Michael Jackson*, *The Beatles*). Names with two or more commas (*Crosby, Stills, Nash & Young*) are band names and aren't turned around. The biography comes in the UI language when last.fm has it, and the link at the end (on macOS, the **Open on Last.fm** button) goes to the artist's page that was found. Artists the server already has a biography for work exactly as before.
* **prefs:** new **Last.fm API key** field in *Preferences › Tools › Navidrome*, under *Language*. The fallback above only runs when a key is set; with the field empty Artist Info behaves as before and nothing is sent to last.fm. A key is free: create one at [last.fm/api/account/create](https://www.last.fm/api/account/create) while signed in to last.fm (an application name and a contact email are enough), and paste the *API key* (not the shared secret). Only the artist name and the UI language are sent to last.fm, from your computer; the custom headers set for your Navidrome server are never sent there. *Top Songs* and *Similar Artists* still come from the server, so for these artist names they may stay empty.


## [1.35.0](https://github.com/santiagorod92/foo_navidrome/compare/v1.34.0...v1.35.0) (2026-10-26)


### Features

* **browser:** **Album Info** in the Navidrome Browser's right-click menu, under *Artist Info*. On an album row it shows that album's notes from last.fm, and on a song row the notes of the song's album, with the link to the album's last.fm page (on macOS, an **Open on Last.fm** button opens it). The notes come from the server (`getAlbumInfo2.view`), which gets them through its last.fm integration: albums last.fm has no description for, or a server without last.fm, show "No album notes available.". Translated into every UI language.


## [1.34.0](https://github.com/santiagorod92/foo_navidrome/compare/v1.33.0...v1.34.0) (2026-10-26)


### Features

* **i18n:** the component speaks **six more languages**: **Português** (Brazilian), **中文** (Simplified Chinese), **日本語**, **Français**, **Deutsch** and **Русский**, on top of English and Spanish. Windows, menus (File › Open Navidrome Browser, the right-click menus), every Preferences page, the Navidrome Browser, the Lyrics panel, popups and status lines are translated. By default the component follows the system language (Windows: the display language; macOS: the first preferred language), so nothing needs setting up; to pick one, use *Preferences › Tools › Navidrome › Language*, where each language is listed by its own name, and restart foobar2000. Any Chinese locale gets Simplified Chinese (Traditional included) and any Portuguese locale gets Brazilian Portuguese. Log lines and *Copy Diagnostics* stay in English. The new translations are a first pass: corrections from native speakers are welcome in an issue. Note that `foobar2000 /command:` takes the menu name in the language shown; keyboard shortcuts keep working in any language.


### Code Refactoring

* **i18n:** one translation table for every language. The Spanish-only table (`I18nSpanish.cpp`) is replaced by `I18nTable.cpp`, where each row is the English text followed by one string per language, in the order of `kLanguages` (`I18n.h`), the single list of languages (code and native name) that also feeds the Language setting and the system-language detection. Adding a language is a `Language` value, a `kLanguages` entry and one more string per row. `make i18n-check` fails on any missing cell in any language, `make i18n-missing ARGS=<code>` lists one language's gaps, the unit tests check every cell is filled and keeps the key's `{n}` placeholders, and the `i18n` UI scenario takes the language to run in (`ARGS='i18n ja'`).


## [1.33.0](https://github.com/santiagorod92/foo_navidrome/compare/v1.32.0...v1.33.0) (2026-10-23)


### Features

* **browser:** search finds **artists and albums**, not just songs. Results in the Navidrome Browser's search field now come in three groups, **Artists (N)**, **Albums (N)** and **Songs (N)**, already expanded. An artist or album found there expands like it does in the tree (with *Top Songs* and *Similar Artists* under an artist), albums read *Artist — Album* with their year, and Add to Playlist / Play Now on a group queues everything in it. The status line counts all three ("9 artists, 20 albums, 50 songs found"). In Spanish the groups read *Artistas*, *Álbumes* and *Canciones*.
* **scrobble:** plays that couldn't be reported are **no longer lost**. When the server can't be reached (it's down, the connection dropped, or you're playing an offline copy), the play is saved in `foo_navidrome-scrobbles.tsv` in the foobar2000 profile and sent with the time it was heard once the server answers again: at the next start, or when the next track reports "now playing". Up to 2000 plays are kept, oldest dropped first. A wrong password or server address keeps them too, so fixing the login sends them. The console says how many were sent. Nothing to set up: it follows *Report plays to Navidrome (scrobbling)*.
* **settings:** **several Navidrome servers**. *Preferences › Tools › Navidrome* has a new **Server:** list with **Add…** and **Remove**: each server keeps its own URL, username, password, custom headers and library selection, and picking one in the list switches to it (open browsers reload). The server you already had shows up under its host name, so nothing needs re-entering. Tracks in your playlists don't record which server they came from: they play from the selected server, so tracks from another server play again once you switch back to it.


## [1.32.0](https://github.com/santiagorod92/foo_navidrome/compare/v1.31.2...v1.32.0) (2026-10-23)


### Features

* **macOS:** [foo_component_updater_mac](https://github.com/alies-dev/foo_component_updater_mac) can update foo_navidrome straight from its GitHub releases ([#44](https://github.com/santiagorod92/foo_navidrome/issues/44)). The component now names its release source in its `Info.plist`, which the updater trusts over the foobar2000.org catalog. foobar2000 itself ignores it. It works from this release on.


## [1.31.2](https://github.com/santiagorod92/foo_navidrome/compare/v1.31.1...v1.31.2) (2026-10-19)


### Bug Fixes

* **browser:** **All Songs** queues your library in album order ([#37](https://github.com/santiagorod92/foo_navidrome/issues/37)). Add to Playlist / Play Now on the *All Songs* node used to queue tracks in whatever order the server returned them, so an album's tracks came out shuffled (and with several libraries, one library after the other). They're now sorted by album artist, year and album, then disc and track number, the same order the *Decades* section uses. Re-add *All Songs* to get the new order; playlists you already built keep theirs.


## [1.31.1](https://github.com/santiagorod92/foo_navidrome/compare/v1.31.0...v1.31.1) (2026-10-19)


### Bug Fixes

* **browser:** **Play Now** in the Navidrome Browser plays what you picked ([#40](https://github.com/santiagorod92/foo_navidrome/issues/40)). With *Playback › Playback follows cursor* off, it used to restart the track that was playing (or the last one played) instead of the selection, and only added the selection to the end of the playlist. Now Play Now (button, right-click menu), double-click, Enter and Random Mix replace the active playlist with the selection and start from its first track, whatever *Playback follows cursor* is set to. **Add to Playlist** still appends without touching playback. The same fix applies to *File › Restore Play Queue from Navidrome* and the AudioMuse-AI playlists, which started playback the same way.


## [1.30.0](https://github.com/santiagorod92/foo_navidrome/compare/v1.29.0...v1.30.0) (2026-10-18)


### Features

* **windows:** the Navidrome Browser's search field can be cleared in one go ([#38](https://github.com/santiagorod92/foo_navidrome/issues/38)). While there's text in it, a **×** button shows beside the field; click it, or press **Esc** in the field, and the full tree is back right away. With an empty field, Esc keeps its usual meaning (for example, closing Preferences when the browser is shown there). macOS's search field already had its own clear button.


## [1.29.0](https://github.com/santiagorod92/foo_navidrome/compare/v1.28.0...v1.29.0) (2026-10-18)


### Features

* **browser:** a new **Decades** section in the Navidrome Browser, right after *Random Albums* ([#37](https://github.com/santiagorod92/foo_navidrome/issues/37)). Expand it for one entry per decade your library spans (*2020s*, *2010s*, … down to your oldest album), then expand a decade for every album released in it, band by band: sorted by artist and then by year, each shown as *Artist — Album* with its year. Add to Playlist / Play Now on a decade queues all of it. Albums without a year aren't listed, and the library filter (*Preferences › Media Library › Navidrome › Libraries*) applies. Like every section, it can be hidden in *Preferences › Tools › Navidrome › Browser Sections*. In Spanish it reads *Décadas* / *Años 1970*. To browse the covers themselves by decade, [Panels UI](https://github.com/santiagorod92/foo_ui_panels) 1.13.0's album browser adds *Sort by › Decade*.


## [1.28.0](https://github.com/santiagorod92/foo_navidrome/compare/v1.27.0...v1.28.0) (2026-10-17)


### Features

* **offline:** **Make Available Offline** keeps a local copy of Navidrome tracks, which then play and seek from disk without the server. It's in the browser's right-click menu (songs, albums, artists, playlists) and on foobar2000 playlist tracks (right-click › *Navidrome*). The original files are downloaded with a progress window you can abort, and a confirmation says how many tracks were added and how much space they take. Copies live in the foobar2000 profile under `foo_navidrome-cache/pinned/` and are never deleted on their own; **Remove Offline Copy** on the same tracks deletes them. On Windows, the copy of the track that's playing is deleted when that track stops, because Windows can't delete a file that's in use.
* **offline:** optional **prefetch**: while a track plays, the next one is downloaded in the background (in the current *Stream as* format), so it starts without buffering and survives a dropped connection. Turn it on in *Preferences › Advanced › Tools › Navidrome: prefetch the next track to a local cache* (off by default). *Navidrome: prefetch cache size (MB)* (default 1024) caps that cache: the oldest files go first, and the two newest are always kept. The next track can only be predicted from the playback queue or with the *Default* and *Repeat (playlist)* orders, so nothing is prefetched in shuffle or random. Turning prefetch off empties its cache at the next start. Scrobbles, ratings and cover art still need the server.
* **language:** the component is now in **English and Spanish**: windows, menus, preferences, status lines and messages. It follows the system language (macOS: the first preferred language; Windows: the display language). To pick one, use the **Language** setting (*Automatic* / *English* / *Español*) on *Preferences › Tools › Navidrome* and restart foobar2000. In Spanish, menu commands have Spanish names, so a script that runs `foobar2000 /command:"Open Navidrome Browser"` needs the Spanish name; keyboard shortcuts keep working. Log lines and *Copy Diagnostics* stay in English, since they're pasted into bug reports.
* **diagnostics:** *Copy Diagnostics* also reports the UI language and the offline cache: pinned files and their size, and whether prefetch is on and how full its cache is.


## [1.27.0](https://github.com/santiagorod92/foo_navidrome/compare/v1.26.0...v1.27.0) (2026-10-15)


### Features

* **windows:** a native **Navidrome Lyrics** panel, like the one macOS already had, for the Default UI and Columns UI. Add it from *View › Layout › Enable layout editing mode* › right-click › *Replace UI Element…* (Default UI) or *Live layout editing* › right-click › *Add after* › *Panels* (Columns UI). It follows the playing Navidrome track. With synced lyrics, the current line is shown in bold and kept centred; if you scroll by hand, it waits a few seconds before following the song again. Unsynced lyrics show as plain text. Right-click › **Copy lyrics** copies them to the clipboard. The panel uses foobar2000's colours and font and follows Dark Mode. ESLyric keeps working for anyone who prefers it.
* **macOS:** **File › Bookmark Current Position** saves the playing track's position to the server, as it already did on Windows. Bookmarks show up under the browser's **Bookmarks** list on every platform.


### Bug Fixes

* **macOS:** on a fresh install, the server URL field was pre-filled with a developer's own server address; it now starts at `http://localhost:4533/`. Existing settings are not touched.
* **windows:** **Download Original Files…** no longer starts when the selection's tracks can't be loaded from the server; the error is shown in the browser's status line instead.
* **preferences:** the *Navidrome: refresh ratings on startup* switch (*Preferences › Advanced › Tools*) shared an internal identifier with another command. It now has its own; if you had turned it off, turn it off once more.


### Code Refactoring

* nearly all the logic now lives in one implementation shared by macOS and Windows: the browser's server access, the `navidrome://` input, album art, scrobbling, the File menu, the settings and every status text. The platform folders only hold the windows and widgets, so fixes and features now land on both platforms at once. No behaviour change beyond the items above.


## [1.26.0](https://github.com/santiagorod92/foo_navidrome/compare/v1.25.1...v1.26.0) (2026-10-14)


### Features

* **metadata:** tracks now carry the server's full tags: **album artist**, **disc number**, every **genre** (as a multi-value field), **composer**, **BPM** and the **MusicBrainz** recording id (`MUSICBRAINZ_TRACKID`), so compilations and multi-disc albums group and sort properly. Tracks already in your playlists keep their old tags; add them again from the browser to pick up the new ones.
* **replaygain:** the server's track/album gain and peak values reach foobar2000, so *Playback › ReplayGain* evens out the volume of streamed tracks like it does for local files (needs an OpenSubsonic server that reports `replayGain`, such as current Navidrome, and files with ReplayGain tags). Placeholder zeros the server sends for untagged files are ignored.
* **streaming:** seeking now works in transcoded streams. A live transcode can't be seeked by foobar2000, so the track is reopened at the new position (needs the OpenSubsonic `transcodeOffset` extension, which current Navidrome has); a stream that can seek keeps seeking normally.
* **play queue sync:** **File › Save Play Queue to Navidrome** stores the playing (or active) playlist, the current track and its position on the server; **File › Restore Play Queue from Navidrome** loads the queue saved there by any Subsonic client (Symfonium, Feishin, Tempo, …) into a *Navidrome Play Queue* playlist and resumes where it left off. Up to 100 tracks are saved, starting at the current one.


### Bug Fixes

* **bookmarks:** resuming a bookmark no longer risks crashing foobar2000 2.26 (*Bug check*): the "seek when ready" wait called playback functions from a background thread. Found while testing play-queue restore, which uses the same code.
* **windows:** in Dark Mode, the column headers of the *Radio Stations* and *Libraries* preference pages were dark grey on a dark background; they are readable now.


## [1.25.1](https://github.com/santiagorod92/foo_navidrome/compare/v1.25.0...v1.25.1) (2026-10-10)


### Bug Fixes

* **windows:** foobar2000 no longer freezes while the Navidrome Browser shows search results or goes back to the full list after a search is cleared ([#30](https://github.com/santiagorod92/foo_navidrome/issues/30)). Rebuilding the tree used to take seconds on large libraries (2.3 s with 1500 artists); it now takes a few milliseconds. Loading the browser and expanding nodes are faster too.


## [1.25.0](https://github.com/santiagorod92/foo_navidrome/compare/v1.24.0...v1.25.0) (2026-10-10)


### Features

* **instant mix:** when Navidrome has no similar songs for a song or an artist and an AudioMuse-AI server URL is set (*Preferences › Tools › Navidrome › AudioMuse-AI*), Instant Mix now asks AudioMuse-AI directly, so it also works without the AudioMuse-AI Navidrome plugin and for tracks last.fm doesn't know ([#26](https://github.com/santiagorod92/foo_navidrome/issues/26)). When nothing is found, the message explains where Navidrome's answer comes from and how to set AudioMuse-AI up.
* **browser:** choose which top-level sections the browser tree shows ([#27](https://github.com/santiagorod92/foo_navidrome/issues/27)). *Preferences › Tools › Navidrome › Browser Sections* has a checkbox per section (All Songs, Starred, Recently Added, Most Played, Recently Played, Random Albums, Genres, Playlists, Bookmarks, Radio, Podcasts, Now Playing); hidden ones disappear from the tree, leaving your artists (and libraries). Open browsers reload on Apply (Windows) or on each click (macOS). Everything stays visible by default.


## [1.24.0](https://github.com/santiagorod92/foo_navidrome/compare/v1.23.0...v1.24.0) (2026-10-10)


### Features

* **windows:** the Navidrome Browser is now available as a [Columns UI](https://github.com/reupen/columns_ui) panel. Turn on *Live layout editing*, right-click a panel › *Add before/after* › *Panels* › **Navidrome Browser**. Nothing extra is needed on the Columns UI side; tested with Columns UI 3.7.0.


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
