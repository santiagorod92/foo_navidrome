#pragma once
#include <string>

namespace navidrome {

// Build a navidrome://track/<id>?title=...&artist=...&album=...&tracknumber=N&
// date=YYYY&duration=SEC&coverArt=...&suffix=mp3&rating=N&starred=1&albumId=... URI.
// Metadata is embedded so playlists render without a network round-trip; the
// input handler resolves the real HTTP stream (with custom headers) at decode
// time. Mirrors the macOS NavidromeMakeTrackURIWithFields builder. rating and
// starred are omitted when unset, so URIs saved by older versions parse as
// unrated/unstarred. albumId is carried for the startup refresh only — it lets
// one getAlbum.view bring a whole album's playlist entries up to date instead of
// one request per track; the input handler never reads it back.
// Returns "" if id is empty.
std::string makeTrackURI(const std::string& id,
                         const std::string& title,
                         const std::string& artist,
                         const std::string& album,
                         int track,
                         int year,
                         double duration,
                         const std::string& coverArtId,
                         const std::string& suffix,
                         int rating,
                         bool starred,
                         const std::string& albumId);

} // namespace navidrome
