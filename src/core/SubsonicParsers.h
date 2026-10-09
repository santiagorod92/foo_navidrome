#pragma once

#include "Json.h"
#include "SubsonicErrors.h"
#include "SubsonicModels.h"
#include <algorithm>
#include <cstdlib>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace navidrome {

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

inline bool jFlag(const json::Value& o, const char* key) { return o.has(key); }

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

inline bool parseBookmark(const json::Value& b, Bookmark& out) {
    auto entries = b["entry"].items();
    if (entries.empty()) return false;
    out.song       = parseSong(*entries[0]);
    out.positionMs = jDouble(b, "position");
    out.comment    = jStr(b, "comment");
    return true;
}

inline PodcastEpisode parsePodcastEpisode(const json::Value& e) {
    PodcastEpisode ep;
    ep.id          = jId(e, "id");
    ep.streamId    = jId(e, "streamId");
    ep.channelId   = jId(e, "channelId");
    ep.title       = jStr(e, "title", "Untitled episode");
    ep.description = jStr(e, "description");
    ep.status      = jStr(e, "status");
    ep.duration    = jDouble(e, "duration");
    return ep;
}

inline PodcastChannel parsePodcastChannel(const json::Value& c) {
    PodcastChannel ch;
    ch.id           = jId(c, "id");
    ch.url          = jStr(c, "url");
    ch.title        = jStr(c, "title", "Untitled podcast");
    ch.description  = jStr(c, "description");
    ch.status       = jStr(c, "status");
    ch.errorMessage = jStr(c, "errorMessage");
    return ch;
}

inline NowPlayingEntry parseNowPlayingEntry(const json::Value& e) {
    NowPlayingEntry np;
    np.song       = parseSong(e);
    np.username   = jStr(e, "username");
    np.minutesAgo = jInt(e, "minutesAgo");
    return np;
}

inline ArtistInfo parseArtistInfo2(const json::Value& inner) {
    ArtistInfo info;
    auto items = inner["artistInfo2"].items();
    if (items.empty()) return info;
    const json::Value& a = *items[0];
    info.biography      = jStr(a, "biography");
    info.musicBrainzId  = jStr(a, "musicBrainzId");
    info.lastFmUrl       = jStr(a, "lastFmUrl");
    info.smallImageUrl   = jStr(a, "smallImageUrl");
    info.mediumImageUrl  = jStr(a, "mediumImageUrl");
    info.largeImageUrl   = jStr(a, "largeImageUrl");
    for (auto* s : a["similarArtist"].items())
        info.similarArtists.push_back(parseArtist(*s));
    return info;
}

inline std::string stripHtmlTags(const std::string& html) {
    std::string out;
    out.reserve(html.size());
    bool inTag = false;
    for (std::size_t i = 0; i < html.size(); ++i) {
        char c = html[i];
        if (c == '<') { inTag = true;  out += ' '; continue; }
        if (c == '>') { inTag = false; continue; }
        if (inTag) continue;
        out += c;
    }
    auto replaceAll = [](std::string& s, const char* from, const char* to) {
        std::size_t pos = 0, fromLen = std::strlen(from);
        while ((pos = s.find(from, pos)) != std::string::npos) {
            s.replace(pos, fromLen, to);
            pos += std::strlen(to);
        }
    };
    replaceAll(out, "&amp;",  "&");
    replaceAll(out, "&lt;",   "<");
    replaceAll(out, "&gt;",   ">");
    replaceAll(out, "&quot;", "\"");
    replaceAll(out, "&#39;",  "'");
    replaceAll(out, "&apos;", "'");
    std::string collapsed;
    collapsed.reserve(out.size());
    bool lastWasSpace = false;
    for (char c : out) {
        bool isSpace = (c == ' ' || c == '\t' || c == '\n' || c == '\r');
        if (isSpace) {
            if (!lastWasSpace && !collapsed.empty()) collapsed += ' ';
            lastWasSpace = true;
        } else {
            collapsed += c;
            lastWasSpace = false;
        }
    }
    while (!collapsed.empty() && collapsed.back() == ' ') collapsed.pop_back();
    return collapsed;
}

inline std::string formatArtistBiography(const ArtistInfo& info) {
    std::string bio = stripHtmlTags(info.biography);
    std::string text = bio.empty() ? "No biography available." : bio;
    if (!info.lastFmUrl.empty()) text += "\n\n" + info.lastFmUrl;
    return text;
}

inline std::vector<std::string> splitLyricText(const std::string& text) {
    std::vector<std::string> out;
    std::size_t pos = 0;
    while (pos <= text.size()) {
        std::size_t nl = text.find('\n', pos);
        std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        out.push_back(std::move(line));
        if (nl == std::string::npos) break;
        pos = nl + 1;
    }
    while (!out.empty() && out.back().empty()) out.pop_back();
    return out;
}

inline std::size_t parseLrcTimeTag(const std::string& s, long long& ms) {
    if (s.size() < 7 || s[0] != '[') return 0;
    std::size_t i = 1;
    long long minutes = 0, seconds = 0, frac = 0, fracDigits = 0;
    auto digit = [&](std::size_t k) { return k < s.size() && s[k] >= '0' && s[k] <= '9'; };
    if (!digit(i)) return 0;
    while (digit(i)) minutes = minutes * 10 + (s[i++] - '0');
    if (i >= s.size() || s[i] != ':') return 0;
    ++i;
    if (!digit(i) || !digit(i + 1)) return 0;
    seconds = (s[i] - '0') * 10 + (s[i + 1] - '0');
    i += 2;
    if (i < s.size() && (s[i] == '.' || s[i] == ':')) {
        ++i;
        while (digit(i) && fracDigits < 3) { frac = frac * 10 + (s[i++] - '0'); ++fracDigits; }
        while (digit(i)) ++i;
    }
    if (i >= s.size() || s[i] != ']') return 0;
    while (fracDigits < 3) { frac *= 10; ++fracDigits; }
    ms = (minutes * 60 + seconds) * 1000 + frac;
    return i + 1;
}

inline Lyrics parseLyricsText(const std::string& text) {
    Lyrics out;
    const auto rawLines = splitLyricText(text);
    std::vector<LyricLine> timed;
    for (const auto& raw : rawLines) {
        std::vector<long long> stamps;
        std::size_t pos = 0;
        long long ms = 0;
        while (std::size_t n = parseLrcTimeTag(raw.substr(pos), ms)) {
            stamps.push_back(ms);
            pos += n;
        }
        for (long long t : stamps) timed.push_back({ t, raw.substr(pos) });
    }
    if (!timed.empty()) {
        std::stable_sort(timed.begin(), timed.end(),
                         [](const LyricLine& a, const LyricLine& b) { return a.startMs < b.startMs; });
        out.synced = true;
        out.lines  = std::move(timed);
        return out;
    }
    for (const auto& raw : rawLines) out.lines.push_back({ -1, raw });
    return out;
}

inline std::vector<Lyrics> parseLyricsList(const json::Value& inner) {
    std::vector<Lyrics> all;
    for (auto* e : inner["lyricsList"]["structuredLyrics"].items()) {
        Lyrics l;
        l.synced = (*e)["synced"].asBool();
        l.lang   = jStr(*e, "lang");
        const long long offset = jLong(*e, "offset");
        for (auto* ln : (*e)["line"].items()) {
            LyricLine line;
            line.text = jStr(*ln, "value");
            if (l.synced) {
                if (!ln->has("start")) continue;
                line.startMs = (std::max)(0LL, jLong(*ln, "start") - offset);
            }
            l.lines.push_back(std::move(line));
        }
        if (l.synced)
            std::stable_sort(l.lines.begin(), l.lines.end(),
                             [](const LyricLine& a, const LyricLine& b) { return a.startMs < b.startMs; });
        if (!l.empty()) all.push_back(std::move(l));
    }
    std::stable_sort(all.begin(), all.end(),
                     [](const Lyrics& a, const Lyrics& b) { return a.synced && !b.synced; });
    return all;
}

inline Lyrics parseLegacyLyrics(const json::Value& inner) {
    auto items = inner["lyrics"].items();
    if (items.empty()) return {};
    return parseLyricsText(jStr(*items[0], "value"));
}

inline int activeLyricLine(const Lyrics& l, long long positionMs) {
    if (!l.synced || l.lines.empty()) return -1;
    auto it = std::upper_bound(l.lines.begin(), l.lines.end(), positionMs,
                               [](long long pos, const LyricLine& line) { return pos < line.startMs; });
    return static_cast<int>(it - l.lines.begin()) - 1;
}

class LyricsCache {
public:
    explicit LyricsCache(std::size_t capacity = 128) : m_capacity(capacity) {}

    bool get(const std::string& songId, Lyrics& out) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& kv : m_items)
            if (kv.first == songId) { out = kv.second; return true; }
        return false;
    }
    void put(const std::string& songId, const Lyrics& l) {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& kv : m_items)
            if (kv.first == songId) { kv.second = l; return; }
        if (m_capacity == 0) return;
        if (m_items.size() >= m_capacity) m_items.erase(m_items.begin());
        m_items.emplace_back(songId, l);
    }
    void clear() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_items.clear();
    }

private:
    std::size_t m_capacity;
    mutable std::mutex m_mutex;
    std::vector<std::pair<std::string, Lyrics>> m_items;
};

inline ScanStatus parseScanStatus(const json::Value& inner) {
    ScanStatus st;
    auto items = inner["scanStatus"].items();
    if (!items.empty()) {
        st.scanning = (*items[0])["scanning"].asBool();
        st.count    = jLong(*items[0], "count");
    }
    return st;
}

struct SubsonicResponse {
    bool        ok = false;
    json::Value root;
    Error       error;

    const json::Value& inner() const { return root["subsonic-response"]; }
};

inline ServerInfo parseServerInfo(const json::Value& inner) {
    ServerInfo i;
    i.apiVersion    = jStr(inner, "version");
    i.type          = jStr(inner, "type");
    i.serverVersion = jStr(inner, "serverVersion");
    i.openSubsonic  = inner["openSubsonic"].asBool(false);
    i.extensionsKnown = !i.openSubsonic;
    return i;
}

inline std::vector<OpenSubsonicExtension> parseOpenSubsonicExtensions(const json::Value& inner) {
    std::vector<OpenSubsonicExtension> out;
    for (auto* e : inner["openSubsonicExtensions"].items()) {
        OpenSubsonicExtension x;
        x.name = jStr(*e, "name");
        if (x.name.empty()) continue;
        for (auto* v : (*e)["versions"].items())
            if (v->type() == json::Value::Number) x.versions.push_back(static_cast<int>(v->asNumber()));
        out.push_back(std::move(x));
    }
    return out;
}

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
}
