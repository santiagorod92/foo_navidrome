#pragma once
// Pure C++ types shared between all platform implementations.
// No ObjC, no Windows headers — safe to include anywhere.

#include <cstddef>
#include <string>
#include <vector>

namespace navidrome {

struct Artist {
    std::string id;
    std::string name;
    std::string coverArtId;
    int albumCount = 0;
    bool starred  = false;
};

struct Album {
    std::string id;
    std::string name;
    std::string artist;
    std::string artistId;
    std::string coverArtId;
    int year      = 0;
    int songCount = 0;
    bool starred  = false;
};

struct Song {
    std::string id;
    std::string title;
    std::string artist;
    std::string artistId;
    std::string album;
    std::string albumId;
    std::string coverArtId;
    std::string suffix;
    int    track    = 0;
    int    year     = 0;
    double duration = 0.0;
    bool   starred  = false;
    int    rating   = 0;   // 0 = unrated, else 1-5
};

// A playlist stored on the server (getPlaylists.view).
struct Playlist {
    std::string id;
    std::string name;
    std::string owner;
    int    songCount = 0;
    double duration  = 0.0;
};

// A genre as reported by getGenres.view. Subsonic names the genre itself
// "value" in the JSON, not "name".
struct Genre {
    std::string name;
    int songCount  = 0;
    int albumCount = 0;
};

// An internet radio station (getInternetRadioStations.view). Unlike every
// other browsable item, playback uses streamUrl directly — no navidrome://
// URI, no transcoding, no server-side resolution needed for a live stream.
struct RadioStation {
    std::string id;
    std::string name;
    std::string streamUrl;
    std::string homePageUrl;   // optional, may be empty
};

// A saved resume position (getBookmarks.view). Subsonic embeds the full song
// object per bookmark, same shape playlists/starred already use, and reports
// position in milliseconds.
struct Bookmark {
    Song   song;
    double positionMs = 0.0;
    std::string comment;
};

// Library scan progress (startScan.view / getScanStatus.view). count is the
// number of items processed so far; only meaningful while scanning is true —
// Subsonic doesn't report a total, so this can only show "N processed", not
// a percentage.
struct ScanStatus {
    bool scanning = false;
    long long count = 0;
};

struct SearchResults {
    std::vector<Artist> artists;
    std::vector<Album>  albums;
    std::vector<Song>   songs;
};

// Item kinds accepted by star.view / unstar.view — Subsonic uses a different
// query parameter name per kind (id / albumId / artistId).
enum class StarKind { Song, Album, Artist };

inline const char* starParamName(StarKind kind) {
    switch (kind) {
        case StarKind::Album:  return "albumId";
        case StarKind::Artist: return "artistId";
        default:               return "id";
    }
}

// getAlbumList2.view "type" values we expose as smart nodes in the browser.
enum class AlbumListType { Newest, Frequent, Recent, Random, Starred };

inline const char* albumListTypeName(AlbumListType type) {
    switch (type) {
        case AlbumListType::Frequent: return "frequent";
        case AlbumListType::Recent:   return "recent";
        case AlbumListType::Random:   return "random";
        case AlbumListType::Starred:  return "starred";
        default:                      return "newest";
    }
}

// Subsonic passes ids on the query string, so a long playlist would blow past
// typical server URL limits (and the Windows client's 4096-wchar WinHttpCrackUrl
// path buffer). Every playlist mutation that takes a list is sent in chunks of
// this many ids.
constexpr std::size_t kPlaylistChunkSize = 50;

// Extra stream.view parameters for the configured transcoding preferences.
// `format` is a Subsonic format name ("mp3", "opus", …), "raw" to force the
// original file, or "" to leave the choice to the server. `maxBitRate` is in
// kbps; 0 means unlimited. Returns a string starting with '&', or "" when
// neither preference is set.
inline std::string streamTranscodeParams(const std::string& format, int maxBitRate) {
    std::string out;
    if (!format.empty())  out += "&format=" + format;
    if (maxBitRate > 0)   out += "&maxBitRate=" + std::to_string(maxBitRate);
    return out;
}

// The codec the server will actually send for the configured format, given the
// track's own suffix. Used as the decoder hint: transcoding to mp3 means a FLAC
// track arrives as mp3, and hinting "track.flac" would pick the wrong decoder.
// "raw" and "" both mean "the original file", so the track's suffix stands.
inline std::string effectiveStreamSuffix(const std::string& format,
                                         const std::string& trackSuffix) {
    if (format.empty() || format == "raw") return trackSuffix;
    return format;
}

// Strip characters that are illegal in Windows / macOS file names, so a track
// title can be used as a download file name. Also trims trailing dots/spaces,
// which Windows silently rejects.
inline std::string sanitizeFileName(const std::string& name) {
    std::string out;
    for (unsigned char c : name) {
        switch (c) {
            case '/': case '\\': case ':': case '*': case '?':
            case '"': case '<':  case '>': case '|':
                out.push_back('_');
                break;
            default:
                out.push_back(static_cast<char>(c < 0x20 ? ' ' : c));
        }
    }
    while (!out.empty() && (out.back() == '.' || out.back() == ' ')) out.pop_back();
    return out.empty() ? std::string("untitled") : out;
}

// Percent-decode a URI component. Ids are opaque server strings that may have
// been escaped; a stray '%' that isn't a valid escape is passed through rather
// than dropped.
inline std::string percentDecode(const std::string& in) {
    std::string out;
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '%' && i + 2 < in.size()) {
            auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            int hi = hex(in[i + 1]), lo = hex(in[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(in[i]);
    }
    return out;
}

// Extract the song id from a navidrome://track/<id>?... URI. Returns "" when
// the path isn't one of ours. Shared so the scrobbler on both platforms maps a
// playing metadb handle back to a Subsonic song id the same way.
inline std::string trackIdFromURI(const std::string& uri) {
    static const std::string prefix = "navidrome://track/";
    if (uri.size() <= prefix.size() || uri.compare(0, prefix.size(), prefix) != 0)
        return std::string();

    std::string id = uri.substr(prefix.size());
    size_t q = id.find('?');
    if (q != std::string::npos) id.erase(q);
    return percentDecode(id);
}

// Read one query parameter out of a navidrome://track/<id>?... URI. Returns ""
// when the URI isn't ours or the parameter is absent — which is also what a URI
// written by an older version looks like, so callers treat "" as "unknown",
// never as a value.
inline std::string queryParamFromURI(const std::string& uri, const std::string& key) {
    static const std::string prefix = "navidrome://track/";
    if (uri.compare(0, prefix.size(), prefix) != 0) return std::string();

    size_t q = uri.find('?');
    if (q == std::string::npos) return std::string();

    const std::string needle = key + "=";
    for (size_t pos = q + 1; pos < uri.size();) {
        size_t amp  = uri.find('&', pos);
        size_t end  = (amp == std::string::npos) ? uri.size() : amp;
        if (uri.compare(pos, needle.size(), needle) == 0)
            return percentDecode(uri.substr(pos + needle.size(), end - pos - needle.size()));
        if (amp == std::string::npos) break;
        pos = amp + 1;
    }
    return std::string();
}

// Parse a multiline custom-headers blob (one "Name: Value" per line) into
// trimmed, non-empty header lines suitable for HTTP request headers. Blank
// lines and lines starting with '#' (treated as comments) are skipped.
// Shared by every platform so API calls and audio streaming send the same set
// (e.g. Cloudflare Access service-token headers for a Zero Trust tunnel).
inline std::vector<std::string> parseHeaderLines(const std::string& blob) {
    std::vector<std::string> out;
    std::string line;
    auto flush = [&]() {
        const char* ws = " \t\r\n";
        size_t b = line.find_first_not_of(ws);
        size_t e = line.find_last_not_of(ws);
        if (b != std::string::npos) {
            std::string trimmed = line.substr(b, e - b + 1);
            if (!trimmed.empty() && trimmed[0] != '#')
                out.push_back(trimmed);
        }
        line.clear();
    };
    for (char ch : blob) {
        if (ch == '\n') flush();
        else            line.push_back(ch);
    }
    flush();
    return out;
}

} // namespace navidrome
