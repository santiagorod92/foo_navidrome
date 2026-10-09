#pragma once

#include <cstdint>
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
    int    rating   = 0;
};

struct Playlist {
    std::string id;
    std::string name;
    std::string owner;
    int    songCount = 0;
    double duration  = 0.0;
};

struct Genre {
    std::string name;
    int songCount  = 0;
    int albumCount = 0;
};

struct MusicFolder {
    std::string id;
    std::string name;
};

struct RadioStation {
    std::string id;
    std::string name;
    std::string streamUrl;
    std::string homePageUrl;
};

struct Bookmark {
    Song   song;
    double positionMs = 0.0;
    std::string comment;
};

struct PodcastEpisode {
    std::string id;
    std::string streamId;
    std::string channelId;
    std::string title;
    std::string description;
    std::string status;
    double      duration = 0.0;
};

struct PodcastChannel {
    std::string id;
    std::string url;
    std::string title;
    std::string description;
    std::string status;
    std::string errorMessage;
};

struct NowPlayingEntry {
    Song        song;
    std::string username;
    int         minutesAgo = 0;
};

struct ArtistInfo {
    std::string biography;
    std::string musicBrainzId;
    std::string lastFmUrl;
    std::string smallImageUrl;
    std::string mediumImageUrl;
    std::string largeImageUrl;
    std::vector<Artist> similarArtists;
};

struct LyricLine {
    long long   startMs = -1;
    std::string text;
};

struct Lyrics {
    bool                   synced = false;
    std::string            lang;
    std::vector<LyricLine> lines;

    bool empty() const { return lines.empty(); }
};

struct ScanStatus {
    bool scanning = false;
    long long count = 0;
};

struct SearchResults {
    std::vector<Artist> artists;
    std::vector<Album>  albums;
    std::vector<Song>   songs;
};

enum class StarKind { Song, Album, Artist };

inline const char* starParamName(StarKind kind) {
    switch (kind) {
        case StarKind::Album:  return "albumId";
        case StarKind::Artist: return "artistId";
        default:               return "id";
    }
}

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

struct OpenSubsonicExtension {
    std::string      name;
    std::vector<int> versions;
};

struct ServerInfo {
    std::string apiVersion;
    std::string type;
    std::string serverVersion;
    bool        openSubsonic = false;
    std::vector<OpenSubsonicExtension> extensions;
    bool        extensionsKnown = false;

    bool hasExtension(const std::string& name) const {
        for (const auto& e : extensions) if (e.name == name) return true;
        return false;
    }
    bool lacksExtension(const std::string& name) const {
        return extensionsKnown && !hasExtension(name);
    }
};

inline std::string describeServer(const ServerInfo& i) {
    std::string out = i.type.empty() ? std::string("Subsonic") : i.type;
    if (!i.serverVersion.empty()) out += " " + i.serverVersion;
    out += ", API " + (i.apiVersion.empty() ? std::string("?") : i.apiVersion);
    if (i.openSubsonic) out += ", OpenSubsonic";
    return out;
}

inline std::string describeExtensions(const ServerInfo& i) {
    if (!i.extensionsKnown) return "unknown";
    if (i.extensions.empty()) return "none";
    std::string out;
    for (const auto& e : i.extensions) {
        if (!out.empty()) out += ", ";
        out += e.name;
        for (std::size_t k = 0; k < e.versions.size(); ++k)
            out += (k == 0 ? " v" : "/v") + std::to_string(e.versions[k]);
    }
    return out;
}
}
