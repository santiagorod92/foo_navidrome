#pragma once
// Pure C++ types shared between all platform implementations.
// No ObjC, no Windows headers — safe to include anywhere.

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
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

// A music folder ("library") from getMusicFolders.view. Subsonic reports the
// id as a JSON number; both clients normalize it to a string so it sits next
// to every other id in the codebase. A server with a single library reports
// exactly one of these — the multi-library filter only engages past that.
struct MusicFolder {
    std::string id;
    std::string name;
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

// ---------------------------------------------------------------------------
// Unified error model — shared by SubsonicClient (macOS) and SubsonicClientWin.
//
// Both HTTP layers previously returned only a free-text error string, so a
// caller couldn't tell "credentials rejected" (deterministic, surface to the
// user) from "connection reset" (transient, retry) from "song not found"
// (skip and move on). ErrorKind is that missing axis; `Error` carries it plus
// the raw HTTP / Subsonic codes and a log-safe message. Pure C++ so it lives
// here, in every build path, and is unit-tested from tests/.
// ---------------------------------------------------------------------------
enum class ErrorKind {
    None,           // success
    NotConfigured,  // no server URL / username / password set yet
    Network,        // DNS failure, connection refused / reset, host unreachable
    Timeout,        // connect / send / receive deadline hit
    Tls,            // certificate or TLS handshake failure
    Auth,           // credentials rejected — HTTP 401/403, Subsonic 40/41/50
    NotFound,       // HTTP 404/410, Subsonic 70
    RateLimited,    // HTTP 429
    ServerError,    // HTTP 5xx, and every Subsonic error code not mapped above
    Parse,          // transport succeeded but the body wasn't valid JSON
    Cancelled,      // request aborted by us
    Unknown,
};

inline const char* errorKindName(ErrorKind kind) {
    switch (kind) {
        case ErrorKind::None:          return "None";
        case ErrorKind::NotConfigured: return "NotConfigured";
        case ErrorKind::Network:      return "Network";
        case ErrorKind::Timeout:      return "Timeout";
        case ErrorKind::Tls:          return "Tls";
        case ErrorKind::Auth:         return "Auth";
        case ErrorKind::NotFound:     return "NotFound";
        case ErrorKind::RateLimited:  return "RateLimited";
        case ErrorKind::ServerError:  return "ServerError";
        case ErrorKind::Parse:        return "Parse";
        case ErrorKind::Cancelled:    return "Cancelled";
        default:                      return "Unknown";
    }
}

// Retry only failures a later identical request could plausibly survive.
// Auth / NotFound / Parse / NotConfigured are deterministic — retrying just
// adds latency before the same failure.
inline bool isRetryable(ErrorKind kind) {
    switch (kind) {
        case ErrorKind::Network:
        case ErrorKind::Timeout:
        case ErrorKind::RateLimited:
        case ErrorKind::ServerError:
            return true;
        default:
            return false;
    }
}

// Map an HTTP status line to an ErrorKind. 2xx -> None. status 0 means "no
// response line at all" (socket died before headers) -> Network. An
// unfollowed 3xx is a transport problem, not a server error.
inline ErrorKind httpStatusToErrorKind(int status) {
    if (status >= 200 && status < 300) return ErrorKind::None;
    if (status == 0)                   return ErrorKind::Network;
    if (status == 401 || status == 403) return ErrorKind::Auth;
    if (status == 404 || status == 410) return ErrorKind::NotFound;
    if (status == 429)                 return ErrorKind::RateLimited;
    if (status >= 500 && status < 600) return ErrorKind::ServerError;
    if (status >= 300 && status < 400) return ErrorKind::Network;
    return ErrorKind::ServerError;
}

// Map a Subsonic <error code="N"> to an ErrorKind. Subsonic's own codes:
//   0  generic      10 missing param     20 client too old   30 server too old
//   40 wrong creds  41 token auth n/a    50 not authorized   60 trial expired
//   70 not found
inline ErrorKind subsonicCodeToErrorKind(int code) {
    switch (code) {
        case 40: case 41: case 50: return ErrorKind::Auth;
        case 70:                   return ErrorKind::NotFound;
        default:                   return ErrorKind::ServerError;
    }
}

struct Error {
    ErrorKind   kind = ErrorKind::None;
    int         http = 0;   // HTTP status, 0 when there was none
    int         code = 0;   // Subsonic error code, 0 when there was none
    std::string message;    // human-readable, already auth-scrubbed — safe to log/show

    bool ok()        const { return kind == ErrorKind::None; }
    bool retryable() const { return isRetryable(kind); }
    const char* kindName() const { return errorKindName(kind); }
};

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

// The transcoding-format and max-bitrate choices offered by the prefs UI on
// both platforms. `value` is the Subsonic `format=` string ("" = server
// default, "raw" = original file); `label` is UTF-8 for the platform to widen /
// NSString-ify. The server only honours a format it has a transcoding row
// configured for — FLAC/WAV need one added in Navidrome's admin UI.
struct StreamFormatOption { const char* label; const char* value; };

inline const std::vector<StreamFormatOption>& streamFormatOptions() {
    static const std::vector<StreamFormatOption> k = {
        { "Server default",            ""     },
        { "Original (no transcoding)", "raw"  },
        { "MP3",                       "mp3"  },
        { "Opus",                      "opus" },
        { "AAC",                       "aac"  },
        { "FLAC (lossless)",           "flac" },
        { "WAV (uncompressed)",        "wav"  },
    };
    return k;
}

// kbps ceilings; 0 = "no limit" (also what Subsonic reads when the param is absent).
inline const std::vector<int>& maxBitrateOptions() {
    static const std::vector<int> k = { 0, 64, 96, 128, 192, 256, 320 };
    return k;
}

// How often the "Rescan Library Now" flow re-polls getScanStatus.view.
constexpr int kScanPollIntervalMs = 1500;

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

// Percent-encode a URI component. Escapes everything outside the RFC 3986
// unreserved set (A-Za-z0-9 and -_.~), uppercase hex — strict, so the output is
// safe in either the path or the query. The inverse of percentDecode above.
inline std::string percentEncode(const std::string& in) {
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(in.size());
    for (unsigned char c : in) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 0x0F]);
        }
    }
    return out;
}

// A decoded navidrome://track/<id>?... URI. Every query field is optional; a URI
// written by an older version simply leaves the newer fields at their defaults,
// so the scheme only ever grows. `rating` 0 means unrated (also: param absent).
// `albumId` is carried for the startup rating refresh only — one getAlbum.view
// brings a whole album's playlist entries up to date; the input handler never
// reads it back.
struct TrackURI {
    std::string id;
    std::string title;
    std::string artist;
    std::string album;
    std::string coverArtId;
    std::string suffix;
    std::string albumId;
    int    track    = 0;
    int    year     = 0;
    int    rating   = 0;
    double duration = 0.0;
    bool   starred  = false;
};

// Build a navidrome://track/<id>?title=...&artist=...&album=...&tracknumber=N&
// date=YYYY&duration=SEC&coverArt=...&suffix=mp3&rating=N&starred=1&albumId=...
// URI. Every field is omitted when unset, so the URI of an unrated, untagged
// track is byte-identical to what earlier versions produced. Returns "" when id
// is empty. Shared by both platforms' enqueue paths.
inline std::string buildTrackURI(const TrackURI& t) {
    if (t.id.empty()) return std::string();
    std::string uri = "navidrome://track/" + percentEncode(t.id);

    std::vector<std::string> q;
    if (!t.title.empty())      q.push_back("title="  + percentEncode(t.title));
    if (!t.artist.empty())     q.push_back("artist=" + percentEncode(t.artist));
    if (!t.album.empty())      q.push_back("album="  + percentEncode(t.album));
    if (t.track > 0)           q.push_back("tracknumber=" + std::to_string(t.track));
    if (t.year > 0)            q.push_back("date="   + std::to_string(t.year));
    if (t.duration > 0.0) {
        char b[32];
        std::snprintf(b, sizeof(b), "%g", t.duration);
        q.push_back(std::string("duration=") + b);
    }
    if (!t.coverArtId.empty()) q.push_back("coverArt=" + percentEncode(t.coverArtId));
    if (!t.suffix.empty())     q.push_back("suffix="   + percentEncode(t.suffix));
    if (t.rating > 0)          q.push_back("rating="   + std::to_string(t.rating));
    if (t.starred)             q.push_back("starred=1");
    if (!t.albumId.empty())    q.push_back("albumId=" + percentEncode(t.albumId));

    for (std::size_t i = 0; i < q.size(); ++i) {
        uri += (i == 0 ? '?' : '&');
        uri += q[i];
    }
    return uri;
}

// Parse a navidrome://track/<id>?... URI. A URI that isn't one of ours yields a
// TrackURI with an empty id (callers treat that as "not ours"). Unknown query
// keys are ignored; an absent key leaves its field at the default, never a
// sentinel — same tolerance trackIdFromURI / queryParamFromURI already give.
inline TrackURI parseTrackURI(const std::string& uri) {
    TrackURI t;
    static const std::string prefix = "navidrome://track/";
    if (uri.size() <= prefix.size() || uri.compare(0, prefix.size(), prefix) != 0)
        return t;

    std::string rest = uri.substr(prefix.size());   // <id>[?query]
    size_t q = rest.find('?');
    if (q == std::string::npos) {
        t.id = percentDecode(rest);
        return t;
    }
    t.id = percentDecode(rest.substr(0, q));
    std::string query = rest.substr(q + 1);

    for (size_t pos = 0; pos < query.size();) {
        size_t amp = query.find('&', pos);
        std::string pair = (amp == std::string::npos)
            ? query.substr(pos) : query.substr(pos, amp - pos);
        size_t eq = pair.find('=');
        std::string k = (eq == std::string::npos) ? pair : pair.substr(0, eq);
        std::string v = (eq == std::string::npos)
            ? std::string() : percentDecode(pair.substr(eq + 1));

        if      (k == "title")       t.title       = v;
        else if (k == "artist")      t.artist      = v;
        else if (k == "album")       t.album       = v;
        else if (k == "tracknumber") t.track       = std::atoi(v.c_str());
        else if (k == "date")        t.year        = std::atoi(v.c_str());
        else if (k == "duration")    t.duration    = std::atof(v.c_str());
        else if (k == "coverArt")    t.coverArtId  = v;
        else if (k == "suffix")      t.suffix      = v;
        else if (k == "rating")      t.rating      = std::atoi(v.c_str());
        else if (k == "starred")     t.starred     = (std::atoi(v.c_str()) != 0);
        else if (k == "albumId")     t.albumId     = v;

        if (amp == std::string::npos) break;
        pos = amp + 1;
    }
    return t;
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

// Read one query parameter out of ANY url string — not just navidrome:// ones.
// Cover-art resolution also sees legacy raw /rest/stream.view HTTP URLs, so this
// scans from the first '?' with no scheme check. Returns the percent-decoded
// value, or "" when the key is absent. A parameter is only matched at a pair
// boundary, so "id" never matches inside "guid=" or "albumId=".
inline std::string rawQueryParam(const std::string& url, const std::string& key) {
    size_t q = url.find('?');
    if (q == std::string::npos) return std::string();
    const std::string needle = key + "=";
    for (size_t pos = q + 1; pos < url.size();) {
        size_t amp = url.find('&', pos);
        size_t end = (amp == std::string::npos) ? url.size() : amp;
        if (end - pos >= needle.size() &&
            url.compare(pos, needle.size(), needle) == 0)
            return percentDecode(url.substr(pos + needle.size(), end - pos - needle.size()));
        if (amp == std::string::npos) break;
        pos = amp + 1;
    }
    return std::string();
}

// Resolve the Subsonic art id for a track path, in priority order: the coverArt=
// query param (album / Folder.jpg id embedded at enqueue time), else the id=
// query param, else the <id> segment of a navidrome://track/<id> URI. Returns ""
// when nothing usable is present. Shared by both platforms' album_art
// extractors — is_our_path there also matches legacy /rest/stream.view URLs, so
// this must handle a plain HTTP url too.
inline std::string resolveArtId(const std::string& path) {
    std::string v = rawQueryParam(path, "coverArt");
    if (!v.empty()) return v;
    v = rawQueryParam(path, "id");
    if (!v.empty()) return v;

    static const std::string prefix = "navidrome://track/";
    if (path.compare(0, prefix.size(), prefix) == 0) {
        size_t begin = prefix.size();
        size_t end   = path.find('?', begin);
        return percentDecode(path.substr(
            begin, end == std::string::npos ? std::string::npos : end - begin));
    }
    return std::string();
}

// Subsonic "submission" (scrobble-complete) fires once the listener has heard
// enough of the track: half its length, capped at 4 minutes. A track of unknown
// length (a live stream, length <= 0) uses the 4-minute cap alone. Shared so
// both scrobblers apply the identical rule — and so neither has to spell the
// Windows-safe (std::min) form.
inline double scrobbleSubmitThreshold(double trackLength) {
    if (trackLength <= 0.0) return 240.0;
    double half = trackLength * 0.5;
    return half < 240.0 ? half : 240.0;
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

// ---------------------------------------------------------------------------
// Multi-library ("music folder") filter — shared by both Subsonic clients.
//
// Navidrome can expose more than one library; getMusicFolders.view lists them.
// Most browse/list/search endpoints accept a single `musicFolderId`, never a
// list, so restricting to a subset means one request per selected id merged
// client-side. These three helpers own the cfg_string <-> id-list conversion
// and the "which ids does this request actually fan out over" decision, so the
// per-platform code stays a thin loop.
// ---------------------------------------------------------------------------

// Parse the cfg_string form (comma-separated folder ids) into trimmed,
// non-empty, de-duplicated entries with their original order preserved.
inline std::vector<std::string> parseMusicFolderIds(const std::string& csv) {
    std::vector<std::string> out;
    std::string cur;
    auto flush = [&]() {
        const char* ws = " \t\r\n";
        size_t b = cur.find_first_not_of(ws);
        size_t e = cur.find_last_not_of(ws);
        if (b != std::string::npos) {
            std::string id = cur.substr(b, e - b + 1);
            if (std::find(out.begin(), out.end(), id) == out.end())
                out.push_back(id);
        }
        cur.clear();
    };
    for (char ch : csv) {
        if (ch == ',') flush();
        else           cur.push_back(ch);
    }
    flush();
    return out;
}

// Serialize an id list back to the cfg_string form.
inline std::string joinMusicFolderIds(const std::vector<std::string>& ids) {
    std::string out;
    for (const auto& id : ids) {
        if (!out.empty()) out += ',';
        out += id;
    }
    return out;
}

// The set of `musicFolderId` values a browse/search request should be fanned
// out over, given the user's filter toggle, their saved selection, and what
// the server currently reports.
//
//   filter disabled                       -> {}   (send no musicFolderId)
//   selection empty                       -> {}
//   server reports < 2 folders            -> {}   (multi-library not in play)
//   selection covers every server folder  -> {}   (1 unfiltered request beats N)
//   otherwise -> selected ids that still exist server-side, in server order
//
// An empty return ALWAYS means "one request, no musicFolderId param" — i.e.
// byte-for-byte today's behaviour. A non-empty return is the fan-out list.
inline std::vector<std::string> effectiveMusicFolderIds(
        bool filterEnabled,
        const std::string& selectedCsv,
        const std::vector<MusicFolder>& serverFolders) {
    if (!filterEnabled)              return {};
    if (serverFolders.size() < 2)   return {};

    std::vector<std::string> selected = parseMusicFolderIds(selectedCsv);
    if (selected.empty())           return {};

    std::vector<std::string> result;
    for (const auto& f : serverFolders) {
        if (std::find(selected.begin(), selected.end(), f.id) != selected.end())
            result.push_back(f.id);
    }
    if (result.empty())                       return {};   // selection all stale
    if (result.size() == serverFolders.size()) return {};  // covers everything
    return result;
}

// Append `&musicFolderId=<id>` to an existing query-params string (which may be
// empty). A blank folderId leaves params untouched — the unfiltered single-
// request path. Folder ids are numeric server-side, so no encoding is needed.
inline std::string appendMusicFolderParam(std::string params,
                                          const std::string& folderId) {
    if (folderId.empty()) return params;
    if (!params.empty()) params += '&';
    params += "musicFolderId=" + folderId;
    return params;
}

// Run `fetch` once per folder id (or once with an empty id when the list is
// empty — the unchanged single-request path), concatenating results and
// dropping later duplicates by `idOf` (order preserved; an empty id is never a
// duplicate). Shared by every fanned-out list endpoint on the Windows client;
// the macOS client keeps its own NSArray/KVC twin (`-fanOutOverFolders:`) since
// it merges ObjC objects, not `navidrome::` structs. Unit-tested (testFanOutMerge).
template <class T, class Fetch, class IdOf>
inline std::vector<T> mergeFanOut(const std::vector<std::string>& folderIds,
                                  Fetch fetch, IdOf idOf) {
    if (folderIds.empty()) return fetch(std::string());
    std::vector<T> merged;
    std::vector<std::string> seen;
    for (const auto& fid : folderIds) {
        std::vector<T> part = fetch(fid);
        for (auto& item : part) {
            std::string id = idOf(item);
            if (id.empty() ||
                std::find(seen.begin(), seen.end(), id) == seen.end()) {
                if (!id.empty()) seen.push_back(id);
                merged.push_back(std::move(item));
            }
        }
    }
    return merged;
}

// getArtist.view ignores `musicFolderId` server-side and its AlbumID3 rows carry
// no library id, so a library-scoped album list can't be built from it directly.
// search3.view *does* honour `musicFolderId`: the caller fans it out over the
// scoped libraries, and this keeps the getArtist album list (order preserved) to
// the ids that search confirmed belong to `artistId`. `searchAlbums` is every
// album search returned (already merged across folders). An empty allow-set
// (odd name, search miss) returns the full list and sets `outUnconfirmed` so the
// caller can log a warning rather than silently hiding everything.
// Unit-tested (testAlbumArtistFilter).
inline std::vector<Album> filterAlbumsByArtistSearch(
        const std::vector<Album>& artistAlbums,
        const std::string& artistId,
        const std::vector<Album>& searchAlbums,
        bool& outUnconfirmed) {
    outUnconfirmed = false;
    std::vector<std::string> allowed;
    for (const auto& a : searchAlbums)
        if (!a.id.empty() && a.artistId == artistId)
            allowed.push_back(a.id);
    if (allowed.empty()) { outUnconfirmed = true; return artistAlbums; }

    std::vector<Album> filtered;
    for (const auto& a : artistAlbums)
        if (std::find(allowed.begin(), allowed.end(), a.id) != allowed.end())
            filtered.push_back(a);
    return filtered;
}

// ---------------------------------------------------------------------------
// HTTP retry policy — shared by SubsonicClientWin::httpGet and the macOS
// client's -fetchInner:. The loop body (WinHTTP vs NSURLSession) and the jitter
// RNG stay platform-side; the attempt count and the backoff formula live here
// so they can't drift apart. Unit-tested (testRetryPolicy).
// ---------------------------------------------------------------------------
namespace retry {

constexpr int kMaxAttempts = 3;

// Retry after a failed attempt? `attempt` is 1-based (the one that just ran).
inline bool again(const Error& e, int attempt) {
    return e.retryable() && attempt < kMaxAttempts;
}

// Milliseconds to wait before attempt #(attempt+1): 300ms * attempt plus the
// caller-supplied jitter in [0, 200). Escalates 0.3s, 0.6s across the retries.
inline int backoffMs(int attempt, int jitterMs) {
    return 300 * attempt + jitterMs;
}

}  // namespace retry

// ---------------------------------------------------------------------------
// Scrobble state machine — shared by both `play_callback_static` scrobblers
// (NavidromePlugin.mm / NavidromePluginWin.cpp). Pure state, no SDK, no
// threading: the platform feeds it playback events and fires the HTTP calls it
// asks for. Centralizes the gate ordering the CLAUDE.md gotcha calls out — the
// rating refresh runs for any of our tracks, the scrobble only when the pref is
// on. Unit-tested (testScrobbleTracker).
// ---------------------------------------------------------------------------
struct ScrobbleTracker {
    struct NewTrackActions {
        std::string refreshRatingId;  // fire the rating refresh — any of our tracks
        std::string scrobbleNowId;    // fire scrobble(id, submission=false) — pref on too
    };

    // On a new track. `rawUri` is the track path (empty if none), `lengthSec`
    // its length, `scrobbleEnabled` the cfg_scrobble pref.
    NewTrackActions onNewTrack(const std::string& rawUri, double lengthSec,
                               bool scrobbleEnabled) {
        songId_.clear();
        length_    = 0.0;
        submitted_ = false;

        NewTrackActions a;
        std::string id = trackIdFromURI(rawUri);
        if (id.empty()) return a;            // not one of ours
        a.refreshRatingId = id;              // display refresh — never gated
        if (!scrobbleEnabled) return a;
        songId_ = id;
        length_ = lengthSec;
        a.scrobbleNowId = id;
        return a;
    }

    // On each playback-time tick. Returns the songId once, when the submission
    // threshold is first crossed; "" otherwise.
    std::string onPlaybackTime(double timeSec) {
        if (songId_.empty() || submitted_) return {};
        if (timeSec < scrobbleSubmitThreshold(length_)) return {};
        submitted_ = true;
        return songId_;
    }

    void onStop() {
        songId_.clear();
        submitted_ = false;
    }

private:
    std::string songId_;
    double      length_    = 0.0;
    bool        submitted_ = false;
};

// ---------------------------------------------------------------------------
// One-line session-env summary for the startup `Env` trace line. Both
// `navidromeLogSessionEnv()` twins gather these values and format them the same
// way; this owns the format. Unit-tested (testSessionEnv).
// ---------------------------------------------------------------------------
struct SessionEnv {
    std::string platform;         // "macOS" / "Windows"
    bool        configured = false;
    std::string serverUrl;
    std::string transcodeFormat;  // "" -> "server-default"
    int         maxBitrate = 0;
    bool        scrobble = false;
    bool        startupRefresh = false;
    bool        customHeaders = false;
};

inline std::string describeSessionEnv(const SessionEnv& e) {
    return "platform=" + e.platform
        + "  configured=" + (e.configured ? "yes" : "no")
        + "  server=" + e.serverUrl
        + "  transcode=" + (e.transcodeFormat.empty() ? "server-default" : e.transcodeFormat)
        + "  maxBitrate=" + std::to_string(e.maxBitrate)
        + "  scrobble=" + (e.scrobble ? "on" : "off")
        + "  startupRefresh=" + (e.startupRefresh ? "on" : "off")
        + "  customHeaders=" + (e.customHeaders ? "yes" : "no");
}

// ---------------------------------------------------------------------------
// Minimal JSON DOM + parser.
//
// Every Subsonic struct on both platforms is built from this one parser: the
// Windows client dropped its `"key":` substring scanner and the macOS client
// stopped calling NSJSONSerialization, so a field name, a default, or a quirk
// like getGenres' "value"-not-"name" is now fixed in exactly one place. Scope
// is deliberately only what Subsonic sends — objects, arrays, strings, numbers,
// true/false/null, shallow nesting; UTF-8 in, UTF-8 out (\uXXXX and surrogate
// pairs decoded). Unit-tested in tests/MediaEnrichmentLogicTests.cpp
// (testJson / testSubsonicParsers).
// ---------------------------------------------------------------------------
namespace json {

class Parser;  // defined below; friended so it can fill Value's private state

class Value {
public:
    enum Type { Null, Bool, Number, String, Array, Object };

    Value() = default;

    Type type() const { return type_; }
    bool isNull()   const { return type_ == Null; }
    bool isObject() const { return type_ == Object; }
    bool isArray()  const { return type_ == Array; }

    bool   asBool(bool def = false)   const { return type_ == Bool   ? b_ : def; }
    double asNumber(double def = 0.0) const { return type_ == Number ? n_ : def; }
    const std::string& asString() const {
        static const std::string kEmpty;
        return type_ == String ? s_ : kEmpty;
    }

    bool has(const std::string& key) const {
        if (type_ != Object) return false;
        for (const auto& kv : obj_) if (kv.first == key) return true;
        return false;
    }

    // Object member lookup; a missing key (or non-object receiver) yields a
    // shared Null, so `v["a"]["b"]` never faults.
    const Value& operator[](const std::string& key) const {
        if (type_ == Object)
            for (const auto& kv : obj_)
                if (kv.first == key) return kv.second;
        return nullRef();
    }
    const Value& operator[](const char* key) const {
        return (*this)[std::string(key)];
    }

    std::size_t size() const {
        return type_ == Array ? arr_.size() : (type_ == Object ? obj_.size() : 0);
    }
    const Value& operator[](std::size_t i) const {
        return (type_ == Array && i < arr_.size()) ? arr_[i] : nullRef();
    }

    // Subsonic collapses a one-element list to a bare object. Walk any "list"
    // field through this: Array -> its elements, Object -> [this], else -> {}.
    std::vector<const Value*> items() const {
        std::vector<const Value*> out;
        if (type_ == Array) {
            out.reserve(arr_.size());
            for (const auto& e : arr_) out.push_back(&e);
        } else if (type_ == Object) {
            out.push_back(this);
        }
        return out;
    }

private:
    static const Value& nullRef() { static const Value kNull; return kNull; }

    Type type_ = Null;
    bool b_ = false;
    double n_ = 0.0;
    std::string s_;
    std::vector<Value> arr_;
    std::vector<std::pair<std::string, Value>> obj_;

    friend class Parser;
};

class Parser {
public:
    // Parse `text`; on failure returns a Null Value and sets `err` non-empty.
    static Value parse(const std::string& text, std::string& err) {
        Parser p(text);
        p.ws();
        Value v = p.value();
        if (!p.err_.empty()) { err = p.err_; return Value(); }
        p.ws();
        if (p.i_ != p.text_.size()) { err = "trailing content after JSON value"; return Value(); }
        err.clear();
        return v;
    }

private:
    explicit Parser(const std::string& t) : text_(t) {}

    const std::string& text_;
    std::size_t i_ = 0;
    std::string err_;

    void fail(const char* m) { if (err_.empty()) err_ = m; }
    bool bad() const { return !err_.empty(); }

    void ws() {
        while (i_ < text_.size()) {
            char c = text_[i_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++i_;
            else break;
        }
    }

    Value value() {
        if (bad()) return Value();
        if (i_ >= text_.size()) { fail("unexpected end of JSON"); return Value(); }
        switch (text_[i_]) {
            case '{': return object();
            case '[': return array();
            case '"': { Value v; v.type_ = Value::String; v.s_ = str(); return v; }
            case 't': case 'f': return boolean();
            case 'n': return null();
            default:  return number();
        }
    }

    Value object() {
        Value v; v.type_ = Value::Object;
        ++i_; ws();                       // consume '{'
        if (i_ < text_.size() && text_[i_] == '}') { ++i_; return v; }
        for (;;) {
            ws();
            if (i_ >= text_.size() || text_[i_] != '"') { fail("expected string key"); return Value(); }
            std::string key = str();
            if (bad()) return Value();
            ws();
            if (i_ >= text_.size() || text_[i_] != ':') { fail("expected ':'"); return Value(); }
            ++i_; ws();
            Value child = value();
            if (bad()) return Value();
            v.obj_.emplace_back(std::move(key), std::move(child));
            ws();
            if (i_ >= text_.size()) { fail("unterminated object"); return Value(); }
            if (text_[i_] == ',') { ++i_; continue; }
            if (text_[i_] == '}') { ++i_; break; }
            fail("expected ',' or '}'"); return Value();
        }
        return v;
    }

    Value array() {
        Value v; v.type_ = Value::Array;
        ++i_; ws();                       // consume '['
        if (i_ < text_.size() && text_[i_] == ']') { ++i_; return v; }
        for (;;) {
            ws();
            Value child = value();
            if (bad()) return Value();
            v.arr_.push_back(std::move(child));
            ws();
            if (i_ >= text_.size()) { fail("unterminated array"); return Value(); }
            if (text_[i_] == ',') { ++i_; continue; }
            if (text_[i_] == ']') { ++i_; break; }
            fail("expected ',' or ']'"); return Value();
        }
        return v;
    }

    Value boolean() {
        if (text_.compare(i_, 4, "true") == 0)  { i_ += 4; Value v; v.type_ = Value::Bool; v.b_ = true;  return v; }
        if (text_.compare(i_, 5, "false") == 0) { i_ += 5; Value v; v.type_ = Value::Bool; v.b_ = false; return v; }
        fail("invalid literal"); return Value();
    }

    Value null() {
        if (text_.compare(i_, 4, "null") == 0) { i_ += 4; return Value(); }
        fail("invalid literal"); return Value();
    }

    Value number() {
        std::size_t start = i_;
        if (i_ < text_.size() && (text_[i_] == '-' || text_[i_] == '+')) ++i_;
        bool any = false;
        while (i_ < text_.size()) {
            char c = text_[i_];
            if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' ||
                c == '+' || c == '-') { any = true; ++i_; }
            else break;
        }
        if (!any) { fail("invalid value"); return Value(); }
        Value v; v.type_ = Value::Number;
        v.n_ = std::strtod(text_.c_str() + start, nullptr);
        return v;
    }

    // Reads a JSON string starting at the opening quote; returns the decoded
    // UTF-8 bytes and leaves i_ just past the closing quote.
    std::string str() {
        std::string out;
        ++i_;                            // consume opening '"'
        while (i_ < text_.size()) {
            char c = text_[i_++];
            if (c == '"') return out;
            if (c != '\\') { out.push_back(c); continue; }
            if (i_ >= text_.size()) break;
            char e = text_[i_++];
            switch (e) {
                case '"':  out.push_back('"');  break;
                case '\\': out.push_back('\\'); break;
                case '/':  out.push_back('/');  break;
                case 'b':  out.push_back('\b'); break;
                case 'f':  out.push_back('\f'); break;
                case 'n':  out.push_back('\n'); break;
                case 'r':  out.push_back('\r'); break;
                case 't':  out.push_back('\t'); break;
                case 'u': {
                    unsigned cp = hex4();
                    if (cp >= 0xD800 && cp <= 0xDBFF &&
                        i_ + 1 < text_.size() && text_[i_] == '\\' && text_[i_ + 1] == 'u') {
                        i_ += 2;
                        unsigned lo = hex4();
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    appendUtf8(out, cp);
                    break;
                }
                default: out.push_back(e); break;
            }
        }
        fail("unterminated string");
        return out;
    }

    unsigned hex4() {
        unsigned v = 0;
        for (int k = 0; k < 4 && i_ < text_.size(); ++k) {
            char c = text_[i_++];
            v <<= 4;
            if      (c >= '0' && c <= '9') v |= unsigned(c - '0');
            else if (c >= 'a' && c <= 'f') v |= unsigned(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= unsigned(c - 'A' + 10);
            else { fail("invalid \\u escape"); return v; }
        }
        return v;
    }

    static void appendUtf8(std::string& out, unsigned cp) {
        if (cp <= 0x7F) {
            out.push_back(char(cp));
        } else if (cp <= 0x7FF) {
            out.push_back(char(0xC0 | (cp >> 6)));
            out.push_back(char(0x80 | (cp & 0x3F)));
        } else if (cp <= 0xFFFF) {
            out.push_back(char(0xE0 | (cp >> 12)));
            out.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(char(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(char(0xF0 | (cp >> 18)));
            out.push_back(char(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(char(0x80 | (cp & 0x3F)));
        }
    }
};

inline Value parse(const std::string& text, std::string& err) {
    return Parser::parse(text, err);
}

}  // namespace json

// ---------------------------------------------------------------------------
// json::Value field accessors + Subsonic object -> struct parsers.
// One implementation each, shared by both HTTP clients.
// ---------------------------------------------------------------------------

inline std::string jStr(const json::Value& o, const char* key,
                        const std::string& def = std::string()) {
    const json::Value& v = o[key];
    return v.type() == json::Value::String ? v.asString() : def;
}

inline int jInt(const json::Value& o, const char* key, int def = 0) {
    const json::Value& v = o[key];
    return v.type() == json::Value::Number ? int(v.asNumber()) : def;
}

inline long long jLong(const json::Value& o, const char* key, long long def = 0) {
    const json::Value& v = o[key];
    return v.type() == json::Value::Number ? static_cast<long long>(v.asNumber()) : def;
}

inline double jDouble(const json::Value& o, const char* key, double def = 0.0) {
    const json::Value& v = o[key];
    return v.type() == json::Value::Number ? v.asNumber() : def;
}

// A field Subsonic may send quoted ("id":"7") or bare ("id":7 — getMusicFolders,
// and some artistId / coverArt forms). Normalized to a string either way.
inline std::string jId(const json::Value& o, const char* key) {
    const json::Value& v = o[key];
    if (v.type() == json::Value::String) return v.asString();
    if (v.type() == json::Value::Number) {
        double d = v.asNumber();
        long long i = static_cast<long long>(d);
        return static_cast<double>(i) == d ? std::to_string(i) : std::to_string(d);
    }
    return std::string();
}

// Subsonic marks a favorite by *emitting* a "starred" timestamp string — the
// value is irrelevant, only that the key is present.
inline bool jFlag(const json::Value& o, const char* key) { return o.has(key); }

// Subsonic reports a song as "song" / "entry" / "child" depending on endpoint;
// the object shape is identical.
inline Song parseSong(const json::Value& s) {
    Song so;
    so.id         = jId(s, "id");
    so.title      = jStr(s, "title", "Unknown Title");
    so.artist     = jStr(s, "artist");
    so.artistId   = jId(s, "artistId");
    so.album      = jStr(s, "album");
    so.albumId    = jId(s, "albumId");
    so.coverArtId = jId(s, "coverArt");
    so.suffix     = jStr(s, "suffix");
    so.track      = jInt(s, "track");
    so.year       = jInt(s, "year");
    so.duration   = jDouble(s, "duration");
    so.starred    = jFlag(s, "starred");
    so.rating     = jInt(s, "userRating");
    return so;
}

inline Album parseAlbum(const json::Value& a) {
    Album al;
    al.id         = jId(a, "id");
    al.name       = jStr(a, "name", "Unknown Album");
    al.artist     = jStr(a, "artist");
    al.artistId   = jId(a, "artistId");
    al.coverArtId = jId(a, "coverArt");
    al.year       = jInt(a, "year");
    al.songCount  = jInt(a, "songCount");
    al.starred    = jFlag(a, "starred");
    return al;
}

inline Artist parseArtist(const json::Value& a) {
    Artist ar;
    ar.id         = jId(a, "id");
    ar.name       = jStr(a, "name", "Unknown Artist");
    ar.coverArtId = jId(a, "coverArt");
    ar.albumCount = jInt(a, "albumCount");
    ar.starred    = jFlag(a, "starred");
    return ar;
}

inline Playlist parsePlaylist(const json::Value& p) {
    Playlist pl;
    pl.id        = jId(p, "id");
    pl.name      = jStr(p, "name", "Unnamed playlist");
    pl.owner     = jStr(p, "owner");
    pl.songCount = jInt(p, "songCount");
    pl.duration  = jDouble(p, "duration");
    return pl;
}

// getGenres.view names the genre string "value", not "name".
inline Genre parseGenre(const json::Value& g) {
    Genre gen;
    gen.name       = jStr(g, "value");
    gen.songCount  = jInt(g, "songCount");
    gen.albumCount = jInt(g, "albumCount");
    return gen;
}

inline MusicFolder parseMusicFolder(const json::Value& f) {
    MusicFolder mf;
    mf.id   = jId(f, "id");
    mf.name = jStr(f, "name");
    return mf;
}

inline RadioStation parseRadioStation(const json::Value& s) {
    RadioStation st;
    st.id          = jId(s, "id");
    st.name        = jStr(s, "name", "Unnamed station");
    st.streamUrl   = jStr(s, "streamUrl");
    st.homePageUrl = jStr(s, "homePageUrl");
    return st;
}

// A bookmark wraps the full song object as "entry" plus a millisecond position.
// Returns false when the bookmark carries no entry (nothing playable).
inline bool parseBookmark(const json::Value& b, Bookmark& out) {
    auto entries = b["entry"].items();
    if (entries.empty()) return false;
    out.song       = parseSong(*entries[0]);
    out.positionMs = jDouble(b, "position");
    out.comment    = jStr(b, "comment");
    return true;
}

// startScan.view / getScanStatus.view: the inner response object carries a
// "scanStatus" object (Subsonic may array-collapse it).
inline ScanStatus parseScanStatus(const json::Value& inner) {
    ScanStatus st;
    auto items = inner["scanStatus"].items();
    if (!items.empty()) {
        st.scanning = (*items[0])["scanning"].asBool();
        st.count    = jLong(*items[0], "count");
    }
    return st;
}

// The parsed top-level Subsonic response. `root` owns the DOM; walk the payload
// off `inner()` (valid only while this struct lives). On any failure `ok` is
// false and `error` says why — same ErrorKind axis both clients already use.
struct SubsonicResponse {
    bool        ok = false;
    json::Value root;
    Error       error;

    const json::Value& inner() const { return root["subsonic-response"]; }
};

inline SubsonicResponse parseSubsonicResponse(const std::string& body) {
    SubsonicResponse r;
    std::string perr;
    r.root = json::parse(body, perr);
    if (!perr.empty()) {
        r.error = { ErrorKind::Parse, 200, 0, "JSON parse failed: " + perr };
        return r;
    }
    const json::Value& inner = r.root["subsonic-response"];
    if (!inner.isObject()) {
        r.error = { ErrorKind::Parse, 200, 0, "response missing subsonic-response wrapper" };
        return r;
    }
    if (jStr(inner, "status") != "ok") {
        auto es = inner["error"].items();
        int code = es.empty() ? 0 : jInt(*es[0], "code");
        std::string msg = es.empty() ? std::string("Unknown Subsonic error")
                                     : jStr(*es[0], "message", "Unknown Subsonic error");
        r.error = { subsonicCodeToErrorKind(code), 200, code, msg };
        return r;
    }
    r.ok = true;
    return r;
}

} // namespace navidrome
