#pragma once
// The browser's "put these nodes in a foobar2000 playlist and maybe start
// playing" step — identical on both platforms, so it lives once here with the
// implementation in main.cpp (which every build path already compiles, same
// rationale as NavidromePlaylistSync.h). Uses the foobar2000 SDK, which is
// cross-platform C++, so nothing here is ObjC or Win32.

#include "NavidromeBrowserModel.h"

#include <functional>
#include <string>
#include <vector>

namespace navidrome {

// Enqueue song / radio-station nodes into the active playlist.
//
//  - Song nodes become navidrome://track/<id>?... URIs so the input handler
//    resolves the real stream (with custom headers) at decode time; metadata
//    hints are pushed so the rows render without a network round-trip.
//  - Radio nodes use their raw stream URL directly (foobar's stock HTTP input);
//    `radioUrl` maps a Radio node's id to that URL from the platform's station
//    cache — return "" to skip the node.
//  - `clearFirst` clears the active playlist before adding (the Enter-to-replace
//    path); otherwise the tracks are appended.
//  - When `play` and at least one track was added, playback starts honoring the
//    user's Playback > Order setting. If exactly one node was passed and it
//    carries a bookmark position, playback resumes there via seekWhenReady().
//
// Main thread only (it touches metadb / playlist_manager / playback_control).
// `statusOut` gets a short user-facing line ("Added N tracks", "No songs
// selected"). Returns the number of tracks actually added.
std::size_t enqueueBrowserNodes(
    const std::vector<BrowserNodePtr>& nodes,
    bool play,
    bool clearFirst,
    const std::function<std::string(const std::string& radioId)>& radioUrl,
    std::string& statusOut);

// Poll playback_can_seek() briefly on a background thread, then seek on the main
// thread — the stream isn't necessarily seekable the instant playback starts.
// Reuse this for any future "jump to position" feature instead of calling
// playback_seek() right after start().
void seekWhenReady(double positionSeconds);

} // namespace navidrome
