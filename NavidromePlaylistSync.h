#pragma once
// Keeps the exported per-user rating / favorite in sync with the server for
// tracks that are already sitting in a foobar2000 playlist. SDK-only, no ObjC
// and no Windows headers — the implementation lives in main.cpp, which every
// platform already compiles.

#include <string>
#include <vector>

namespace navidrome {

// Title-formatting field names. Prefixed on purpose — foobar2000's Playback
// Statistics owns %rating% (see CLAUDE.md).
constexpr const char* kRatingTag  = "NAVIDROME_RATING";
constexpr const char* kStarredTag = "NAVIDROME_STARRED";

// One song's current server-side per-user state.
struct RatingUpdate {
    std::string songId;
    int  rating  = 0;       // 0 = unrated
    bool starred = false;
};

// Pushes these values onto every playlist entry whose navidrome:// URI carries
// a matching song id, updating the entries in place via a forced hint. The URI
// is never rewritten — it is the metadb identity, so that would duplicate the
// entry rather than update it (see CLAUDE.md).
//
// Safe to call from any thread; the playlist walk is marshalled to the main
// thread. Unchanged entries are skipped, so a no-op sync causes no repaint.
void syncRatingsToPlaylists(std::vector<RatingUpdate> updates);

// What a walk of every playlist found, for the startup refresh. One
// getAlbum.view per distinct albumId covers a whole album at once, which is
// what makes a full refresh affordable; `ungrouped` counts the entries written
// before the URI carried an albumId, which can't be grouped and are reported
// rather than silently dropped (see CLAUDE.md).
struct PlaylistAlbumScan {
    std::vector<std::string> albumIds;   // distinct, in first-seen order
    std::size_t entries   = 0;           // navidrome:// entries seen in total
    std::size_t ungrouped = 0;           // of those, ones carrying no albumId
};

// Main thread only — it walks the playlists.
PlaylistAlbumScan scanPlaylistAlbums();

// Startup-refresh switch, under Preferences › Advanced › Tools. Default on.
bool refreshRatingsOnStartEnabled();

} // namespace navidrome
