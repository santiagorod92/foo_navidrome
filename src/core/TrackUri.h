#pragma once

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "TrackTags.h"

namespace navidrome {

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
    TrackTags tags;
};

inline std::string formatUriNumber(double v) {
    char b[32];
    std::snprintf(b, sizeof(b), "%.6g", v);
    return b;
}

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

    const TrackTags& g = t.tags;
    if (!g.albumArtist.empty())   q.push_back("albumArtist=" + percentEncode(g.albumArtist));
    if (!g.composer.empty())      q.push_back("composer=" + percentEncode(g.composer));
    for (const auto& genre : g.genres)
        if (!genre.empty())       q.push_back("genre=" + percentEncode(genre));
    if (g.discNumber > 0)         q.push_back("disc=" + std::to_string(g.discNumber));
    if (g.bpm > 0)                q.push_back("bpm=" + std::to_string(g.bpm));
    if (!g.musicBrainzId.empty()) q.push_back("mbid=" + percentEncode(g.musicBrainzId));
    const ReplayGain& rg = g.replayGain;
    if (rg.hasTrackGain) q.push_back("rgTrackGain=" + formatUriNumber(rg.trackGain));
    if (rg.hasAlbumGain) q.push_back("rgAlbumGain=" + formatUriNumber(rg.albumGain));
    if (rg.hasTrackPeak) q.push_back("rgTrackPeak=" + formatUriNumber(rg.trackPeak));
    if (rg.hasAlbumPeak) q.push_back("rgAlbumPeak=" + formatUriNumber(rg.albumPeak));

    for (std::size_t i = 0; i < q.size(); ++i) {
        uri += (i == 0 ? '?' : '&');
        uri += q[i];
    }
    return uri;
}

inline TrackURI parseTrackURI(const std::string& uri) {
    TrackURI t;
    static const std::string prefix = "navidrome://track/";
    if (uri.size() <= prefix.size() || uri.compare(0, prefix.size(), prefix) != 0)
        return t;

    std::string rest = uri.substr(prefix.size());
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
        else if (k == "albumArtist") t.tags.albumArtist   = v;
        else if (k == "composer")    t.tags.composer      = v;
        else if (k == "genre")       { if (!v.empty()) t.tags.genres.push_back(v); }
        else if (k == "disc")        t.tags.discNumber    = std::atoi(v.c_str());
        else if (k == "bpm")         t.tags.bpm           = std::atoi(v.c_str());
        else if (k == "mbid")        t.tags.musicBrainzId = v;
        else if (k == "rgTrackGain") { t.tags.replayGain.trackGain = std::atof(v.c_str()); t.tags.replayGain.hasTrackGain = true; }
        else if (k == "rgAlbumGain") { t.tags.replayGain.albumGain = std::atof(v.c_str()); t.tags.replayGain.hasAlbumGain = true; }
        else if (k == "rgTrackPeak") { t.tags.replayGain.trackPeak = std::atof(v.c_str()); t.tags.replayGain.hasTrackPeak = true; }
        else if (k == "rgAlbumPeak") { t.tags.replayGain.albumPeak = std::atof(v.c_str()); t.tags.replayGain.hasAlbumPeak = true; }

        if (amp == std::string::npos) break;
        pos = amp + 1;
    }
    return t;
}

inline std::vector<std::pair<std::string, std::string>> trackMetaFields(const TrackURI& t) {
    std::vector<std::pair<std::string, std::string>> out;
    if (!t.title.empty())  out.emplace_back("title", t.title);
    if (!t.artist.empty()) out.emplace_back("artist", t.artist);
    if (!t.album.empty())  out.emplace_back("album", t.album);
    if (t.track > 0)       out.emplace_back("tracknumber", std::to_string(t.track));
    if (t.year > 0)        out.emplace_back("date", std::to_string(t.year));
    for (auto& f : trackTagMetaFields(t.tags)) out.push_back(std::move(f));
    return out;
}

inline std::string trackIdFromURI(const std::string& uri) {
    static const std::string prefix = "navidrome://track/";
    if (uri.size() <= prefix.size() || uri.compare(0, prefix.size(), prefix) != 0)
        return std::string();

    std::string id = uri.substr(prefix.size());
    size_t q = id.find('?');
    if (q != std::string::npos) id.erase(q);
    return percentDecode(id);
}

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

inline bool isNavidromeArtPath(const char* path) {
    if (!path) return false;
    if (std::strncmp(path, "navidrome://", 12) == 0) return true;
    return std::strstr(path, "/rest/stream.view") != nullptr;
}
}
