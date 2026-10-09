#pragma once
#include "NavidromeBrowserModel.h"
#include "SubsonicCore.h"
#include "NavidromeDebugLog.h"

#include <cstdio>
#include <string>
#include <vector>

namespace navidrome {

namespace audiomuse {

constexpr int kDefaultCount = 50;
constexpr int kMaxCount     = 500;
constexpr int kSearchTimeoutMs   = 60000;
constexpr int kPlaylistTimeoutMs = 300000;

inline int clampCount(int n) {
    if (n < 1) return kDefaultCount;
    return n > kMaxCount ? kMaxCount : n;
}

struct Settings {
    std::string url;
    std::string token;
    std::string server;
    int         count = kDefaultCount;
    bool configured() const { return !url.empty(); }
};

struct Track {
    std::string id;
    std::string title;
    std::string artist;
    std::string album;
};

struct AlchemySeed {
    std::string id;
    bool artist   = false;
    bool subtract = false;
};

struct IJsonPoster {
    virtual ~IJsonPoster() = default;
    virtual HttpResult postJson(const std::string& url, const std::string& body,
                                const std::string& bearerToken, int timeoutMs) = 0;
};

inline std::string jsonQuote(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    out += '"';
    return out;
}

inline std::string endpointURL(const std::string& base, const char* path) {
    if (base.empty()) return {};
    std::string b = base;
    while (!b.empty() && b.back() == '/') b.pop_back();
    return b + path;
}

inline std::string serverField(const std::string& server) {
    return server.empty() ? std::string() : ",\"server\":" + jsonQuote(server);
}

inline std::string textSearchBody(const std::string& query, int n, const std::string& server) {
    return "{\"query\":" + jsonQuote(query) + ",\"limit\":" + std::to_string(clampCount(n)) +
           serverField(server) + "}";
}

inline std::string instantPlaylistBody(const std::string& prompt, int n, const std::string& server) {
    return "{\"userInput\":" + jsonQuote(prompt) + ",\"n\":" + std::to_string(clampCount(n)) +
           serverField(server) + "}";
}

inline std::string alchemyBody(const std::vector<AlchemySeed>& seeds, int n,
                               const std::string& server) {
    std::string items;
    for (const auto& s : seeds) {
        if (s.id.empty()) continue;
        if (!items.empty()) items += ',';
        items += "{\"id\":" + jsonQuote(s.id) + ",\"op\":\"" + (s.subtract ? "SUBTRACT" : "ADD") +
                 "\",\"type\":\"" + (s.artist ? "artist" : "song") + "\"}";
    }
    return "{\"items\":[" + items + "],\"n\":" + std::to_string(clampCount(n)) +
           serverField(server) + "}";
}

enum class Kind { TextSearch, InstantPlaylist, Alchemy };

inline const char* kindName(Kind k) {
    switch (k) {
        case Kind::TextSearch:      return "Text Search";
        case Kind::InstantPlaylist: return "Instant Playlist";
        case Kind::Alchemy:         return "Song Alchemy";
    }
    return "?";
}

inline std::string errorMessage(const HttpResult& r) {
    std::string parseErr;
    const json::Value root = json::parse(r.body, parseErr);
    for (const char* key : { "error_message", "error", "message" }) {
        const std::string& m = root[key].asString();
        if (!m.empty()) return m;
    }
    if (!r.error.message.empty()) return r.error.message;
    return "request failed";
}

inline std::vector<Track> parseTracks(const std::string& body, Kind kind, std::string& outError) {
    outError.clear();
    std::string parseErr;
    const json::Value root = json::parse(body, parseErr);
    if (!root.isObject()) { outError = "AudioMuse-AI returned an invalid response"; return {}; }

    const json::Value& list = kind == Kind::InstantPlaylist
        ? root["response"]["query_results"] : root["results"];

    std::vector<Track> out;
    for (const json::Value* row : list.items()) {
        Track t;
        const json::Value& id = (*row)["item_id"];
        t.id = id.type() == json::Value::Number
                   ? std::to_string(static_cast<long long>(id.asNumber())) : id.asString();
        if (t.id.empty()) continue;
        t.title  = (*row)["title"].asString();
        t.artist = (*row)["author"].asString();
        if (t.artist.empty()) t.artist = (*row)["artist"].asString();
        t.album  = (*row)["album"].asString();
        out.push_back(std::move(t));
    }
    if (out.empty() && kind == Kind::InstantPlaylist) {
        const std::string& msg = root["response"]["message"].asString();
        if (!root["error"].asString().empty()) outError = root["error"].asString();
        else if (msg.empty()) outError = "AudioMuse-AI found no songs";
        else {
            std::string last;
            std::size_t start = 0;
            while (start <= msg.size()) {
                std::size_t end = msg.find('\n', start);
                if (end == std::string::npos) end = msg.size();
                std::string line = msg.substr(start, end - start);
                if (line.find_first_not_of(" \t\r") != std::string::npos) last = line;
                start = end + 1;
            }
            outError = "AudioMuse-AI found no songs: " + last;
        }
    }
    return out;
}

inline std::vector<Track> request(IJsonPoster& http, const Settings& settings, Kind kind,
                                  const std::string& body, std::string& outError) {
    outError.clear();
    if (!settings.configured()) {
        outError = "AudioMuse-AI is not configured (Preferences > Tools > Navidrome > AudioMuse-AI)";
        return {};
    }
    const char* path = kind == Kind::TextSearch      ? "/api/clap/search"
                     : kind == Kind::InstantPlaylist ? "/chat/api/chatPlaylist"
                                                     : "/api/alchemy";
    const std::string url = endpointURL(settings.url, path);
    const int timeout = kind == Kind::InstantPlaylist ? kPlaylistTimeoutMs : kSearchTimeoutMs;

    NAVIDROME_LOG("AudioMuse", std::string(kindName(kind)) + " POST " + dbg::scrubAuth(url));
    const HttpResult r = http.postJson(url, body, settings.token, timeout);
    if (!r.error.ok()) {
        outError = std::string(kindName(kind)) + ": " + errorMessage(r);
        NAVIDROME_WARN("AudioMuse", outError + " (" + errorKindName(r.error.kind) +
                       ", HTTP " + std::to_string(r.error.http) + ")");
        return {};
    }
    auto tracks = parseTracks(r.body, kind, outError);
    if (!outError.empty()) NAVIDROME_WARN("AudioMuse", std::string(kindName(kind)) + ": " + outError);
    else NAVIDROME_LOG("AudioMuse", std::string(kindName(kind)) + ": " +
                       std::to_string(tracks.size()) + " tracks");
    return tracks;
}

inline std::vector<Track> textSearch(IJsonPoster& http, const Settings& s,
                                     const std::string& query, std::string& outError) {
    return request(http, s, Kind::TextSearch, textSearchBody(query, s.count, s.server), outError);
}

inline std::vector<Track> instantPlaylist(IJsonPoster& http, const Settings& s,
                                          const std::string& prompt, std::string& outError) {
    return request(http, s, Kind::InstantPlaylist,
                   instantPlaylistBody(prompt, s.count, s.server), outError);
}

inline std::vector<Track> alchemy(IJsonPoster& http, const Settings& s,
                                  const std::vector<AlchemySeed>& seeds, std::string& outError) {
    bool anyAdd = false;
    for (const auto& seed : seeds) anyAdd = anyAdd || (!seed.subtract && !seed.id.empty());
    if (!anyAdd) { outError = "Song Alchemy needs at least one song or artist"; return {}; }
    return request(http, s, Kind::Alchemy, alchemyBody(seeds, s.count, s.server), outError);
}

inline std::vector<BrowserNodePtr> resolveTracks(IBrowserClient& client,
                                                 const std::vector<Track>& tracks,
                                                 std::size_t& unresolved) {
    unresolved = 0;
    std::vector<BrowserNodePtr> out;
    out.reserve(tracks.size());
    for (const auto& t : tracks) {
        Song s;
        std::string err;
        if (client.getSong(t.id, s, err) && !s.id.empty()) {
            out.push_back(makeSongNode(s));
        } else {
            ++unresolved;
            NAVIDROME_WARN("AudioMuse", "getSong " + t.id + " (" + t.title + ") failed: " + err);
        }
    }
    return out;
}

inline std::vector<AlchemySeed> seedsFromNodes(const std::vector<BrowserNodePtr>& nodes,
                                               std::string& label) {
    std::vector<AlchemySeed> seeds;
    label.clear();
    for (const auto& n : nodes) {
        if (!n || n->id.empty()) continue;
        if (n->type != BrowserNode::Song && n->type != BrowserNode::Artist) continue;
        AlchemySeed s;
        s.id = n->id;
        s.artist = n->type == BrowserNode::Artist;
        if (seeds.empty()) label = n->displayName;
        seeds.push_back(std::move(s));
    }
    if (seeds.size() > 1) label += " + " + std::to_string(seeds.size() - 1) + " more";
    return seeds;
}

inline std::string playlistName(Kind kind, const std::string& subject) {
    const std::string prefix = kind == Kind::Alchemy ? "AudioMuse Alchemy" : "AudioMuse";
    if (subject.empty()) return prefix;
    std::string s = subject;
    if (s.size() > 60) {
        std::size_t cut = 57;
        while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
        s = s.substr(0, cut) + "...";
    }
    return prefix + ": " + s;
}
}
}
