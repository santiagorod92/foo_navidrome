#pragma once
#include "NavidromeBrowserModel.h"
#include "SubsonicCore.h"
#include "NavidromeDebugLog.h"
#include "SmartFade.h"

#include <cstdio>
#include <string>
#include <vector>

namespace navidrome {

namespace audiomuse {

constexpr int kDefaultCount = 50;
constexpr int kMaxCount     = 500;
constexpr int kSearchTimeoutMs   = 60000;
constexpr int kPlaylistTimeoutMs = 300000;
constexpr int kTransitionTimeoutMs = 300000;

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

struct FadePoint {
    std::string id;
    bool   timed    = false;
    double fadeIn   = 0.0;
    double fadeOut  = 0.0;
    double duration = 0.0;
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
    virtual HttpResult getJson(const std::string& url, const std::string& bearerToken, int timeoutMs) = 0;
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

inline std::string byTrackBody(const std::string& id, int n, const std::string& server) {
    return "{\"item_id\":" + jsonQuote(id) + ",\"n_results\":" + std::to_string(clampCount(n)) +
           serverField(server) + "}";
}

inline std::string smartFadeBody(const std::vector<std::string>& ids, const std::string& server) {
    std::string items;
    for (const auto& id : ids) {
        if (id.empty()) continue;
        if (!items.empty()) items += ',';
        items += jsonQuote(id);
    }
    return "{\"item_ids\":[" + items + "]" + serverField(server) + "}";
}

enum class Kind { TextSearch, InstantPlaylist, Alchemy, InstantMix, SmartFade };

inline const char* kindName(Kind k) {
    switch (k) {
        case Kind::TextSearch:      return N_("Text Search");
        case Kind::InstantPlaylist: return N_("Instant Playlist");
        case Kind::Alchemy:         return N_("Song Alchemy");
        case Kind::InstantMix:      return N_("Instant Mix");
        case Kind::SmartFade:       return N_("Smart Fade");
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
    return tr("request failed");
}

inline std::vector<Track> parseTracks(const std::string& body, Kind kind, std::string& outError) {
    outError.clear();
    std::string parseErr;
    const json::Value root = json::parse(body, parseErr);
    if (!root.isObject()) { outError = tr("AudioMuse-AI returned an invalid response"); return {}; }

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
        else if (msg.empty()) outError = tr("AudioMuse-AI found no songs");
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
            outError = trf("AudioMuse-AI found no songs: {0}", { last });
        }
    }
    return out;
}

inline bool post(IJsonPoster& http, const Settings& settings, Kind kind, const char* path,
                 const std::string& body, std::string& outBody, std::string& outError) {
    outError.clear();
    if (!settings.configured()) {
        outError = tr("AudioMuse-AI is not configured (Preferences > Tools > Navidrome > AudioMuse-AI)");
        return false;
    }
    const std::string url = endpointURL(settings.url, path);
    const int timeout = kind == Kind::InstantPlaylist ? kPlaylistTimeoutMs : kSearchTimeoutMs;

    NAVIDROME_LOG("AudioMuse", std::string(kindName(kind)) + " POST " + dbg::scrubAuth(url));
    const HttpResult r = http.postJson(url, body, settings.token, timeout);
    if (!r.error.ok()) {
        outError = std::string(tr(kindName(kind))) + ": " + errorMessage(r);
        NAVIDROME_WARN("AudioMuse", outError + " (" + errorKindName(r.error.kind) +
                       ", HTTP " + std::to_string(r.error.http) + ")");
        return false;
    }
    outBody = r.body;
    return true;
}

inline std::vector<Track> request(IJsonPoster& http, const Settings& settings, Kind kind,
                                  const std::string& body, std::string& outError) {
    const char* path = kind == Kind::TextSearch      ? "/api/clap/search"
                     : kind == Kind::InstantPlaylist ? "/chat/api/chatPlaylist"
                     : kind == Kind::SmartFade       ? "/api/recording_search/by_track"
                                                     : "/api/alchemy";
    std::string response;
    if (!post(http, settings, kind, path, body, response, outError)) return {};
    auto tracks = parseTracks(response, kind, outError);
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
    if (!anyAdd) { outError = tr("Song Alchemy needs at least one song or artist"); return {}; }
    return request(http, s, Kind::Alchemy, alchemyBody(seeds, s.count, s.server), outError);
}

enum class MixFallback { NotConfigured, Unsupported, Failed, Empty };

inline bool canMixFrom(const BrowserNode::Type seedType) {
    return seedType == BrowserNode::Song || seedType == BrowserNode::Artist;
}

inline std::vector<Track> similarTo(IJsonPoster& http, const Settings& s, const std::string& seedId,
                                    BrowserNode::Type seedType, std::string& outError) {
    outError.clear();
    if (!canMixFrom(seedType) || seedId.empty()) {
        outError = tr("AudioMuse-AI can only mix from a song or an artist");
        return {};
    }
    NAVIDROME_LOG("AudioMuse", "Instant Mix fallback: seed=" + seedId +
                  (seedType == BrowserNode::Artist ? " (artist)" : " (song)"));
    AlchemySeed seed;
    seed.id = seedId;
    seed.artist = seedType == BrowserNode::Artist;
    return request(http, s, Kind::InstantMix, alchemyBody({ seed }, s.count, s.server), outError);
}

inline std::vector<FadePoint> parseFadePoints(const std::string& body, std::string& outError) {
    outError.clear();
    std::string parseErr;
    const json::Value root = json::parse(body, parseErr);
    if (!root.isObject()) { outError = tr("AudioMuse-AI returned an invalid response"); return {}; }
    std::vector<FadePoint> out;
    for (const json::Value* row : root["results"].items()) {
        FadePoint p;
        p.id = (*row)["item_id"].asString();
        if (p.id.empty()) continue;
        const json::Value& in = (*row)["fade_in_seconds"];
        const json::Value& fadeOut = (*row)["fade_out_seconds"];
        p.timed = in.type() == json::Value::Number && fadeOut.type() == json::Value::Number;
        if (p.timed) {
            p.fadeIn  = in.asNumber();
            p.fadeOut = fadeOut.asNumber();
        }
        if ((*row)["duration_seconds"].type() == json::Value::Number)
            p.duration = (*row)["duration_seconds"].asNumber();
        out.push_back(std::move(p));
    }
    if (out.empty()) outError = tr("AudioMuse-AI found no songs");
    return out;
}

inline std::vector<FadePoint> smartFade(IJsonPoster& http, const Settings& s,
                                        std::vector<std::string> ids, std::string& outError) {
    outError.clear();
    if (ids.empty()) { outError = tr("Smart Fade needs songs"); return {}; }
    if (ids.size() == 1) {
        NAVIDROME_LOG("AudioMuse", "Smart Fade: seed=" + ids.front());
        const auto similar = request(http, s, Kind::SmartFade, byTrackBody(ids.front(), s.count, s.server), outError);
        if (!outError.empty()) return {};
        for (const auto& t : similar)
            if (t.id != ids.front()) ids.push_back(t.id);
        if (ids.size() == 1) { outError = tr("AudioMuse-AI found no songs"); return {}; }
    }
    std::string response;
    if (!post(http, s, Kind::SmartFade, "/api/recording_search/smart_fade",
              smartFadeBody(ids, s.server), response, outError)) return {};
    auto points = parseFadePoints(response, outError);
    if (!outError.empty()) NAVIDROME_WARN("AudioMuse", "Smart Fade: " + outError);
    else NAVIDROME_LOG("AudioMuse", "Smart Fade: " + std::to_string(points.size()) + " tracks ordered");
    return points;
}

inline std::string transitionURL(const Settings& s, const std::string& fromId, const std::string& toId,
                                 double outSeconds, double inSeconds) {
    const std::string base = endpointURL(s.url, "/api/recording_search/transition");
    if (base.empty()) return {};
    return base + "?from_id=" + percentEncode(fromId) + "&to_id=" + percentEncode(toId) +
           "&out=" + formatUriNumber(outSeconds) + "&in=" + formatUriNumber(inSeconds);
}

inline std::vector<double> numberList(const json::Value& v) {
    std::vector<double> out;
    for (const json::Value* item : v.items())
        if (item->type() == json::Value::Number) out.push_back(item->asNumber());
    return out;
}

inline void parseSongCurves(const json::Value& v, smartfade::SongCurves& out) {
    if (v["gain"].type() == json::Value::Number) out.gain = v["gain"].asNumber();
    out.speed = numberList(v["speed"]);
    out.volume = numberList(v["volume"]);
    out.highDb = numberList(v["high_db"]);
    out.midDb = numberList(v["mid_db"]);
    out.bassDb = numberList(v["bass_db"]);
    if (v["highpass_hz"].isArray()) out.highpassHz = numberList(v["highpass_hz"]);
}

inline std::string bodyExcerpt(const std::string& body) {
    std::string s = body.substr(0, 200);
    for (char& c : s)
        if (c == '\n' || c == '\r' || c == '\t') c = ' ';
    return body.size() > 200 ? s + "..." : s;
}

inline bool parseTransition(const std::string& body, const std::string& fromId, const std::string& toId,
                            smartfade::Transition& out, std::string& outError) {
    outError.clear();
    std::string parseErr;
    const json::Value root = json::parse(body, parseErr);
    if (!root.isObject()) { outError = "not a JSON object: " + bodyExcerpt(body); return false; }
    if (root["type"].type() == json::Value::String) {
        const char* keys[] = { "start", "duration", "step" };
        for (const char* k : keys)
            if (root[k].type() != json::Value::Number) { outError = std::string("missing ") + k + ": " + bodyExcerpt(body); return false; }
        const json::Value& to = root["to"];
        if (to["start"].type() != json::Value::Number || to["continue_from"].type() != json::Value::Number) {
            outError = "missing to.start or to.continue_from: " + bodyExcerpt(body);
            return false;
        }
        out = smartfade::Transition{};
        out.fromId = fromId;
        out.toId = toId;
        out.type = root["type"].asString();
        out.start = root["start"].asNumber();
        out.duration = root["duration"].asNumber();
        out.step = root["step"].asNumber();
        const json::Value& f = root["filters"];
        out.highHz = f["high_hz"].asNumber(out.highHz);
        out.midHz = f["mid_hz"].asNumber(out.midHz);
        out.midQ = f["mid_q"].asNumber(out.midQ);
        out.bassHz = f["bass_hz"].asNumber(out.bassHz);
        parseSongCurves(root["from"], out.from);
        parseSongCurves(to, out.to);
        out.toStart = to["start"].asNumber();
        out.continueFrom = to["continue_from"].asNumber();
    } else {
        const char* keys[] = { "mix_at", "mix_seconds", "in_at", "resume_at" };
        for (const char* k : keys)
            if (root[k].type() != json::Value::Number) { outError = "unexpected answer: " + bodyExcerpt(body); return false; }
        out = smartfade::legacyTransition(fromId, toId, root["mix_at"].asNumber(), root["mix_seconds"].asNumber(),
                                          root["in_at"].asNumber(), root["resume_at"].asNumber());
    }
    if (!out.valid()) { outError = "transition out of range: " + bodyExcerpt(body); return false; }
    return true;
}

inline bool transition(IJsonPoster& http, const Settings& s, const std::string& fromId, const std::string& toId,
                       double outSeconds, double inSeconds, smartfade::Transition& out, std::string& outError) {
    outError.clear();
    if (!s.configured()) { outError = "AudioMuse-AI is not configured"; return false; }
    const std::string url = transitionURL(s, fromId, toId, outSeconds, inSeconds);
    NAVIDROME_LOG("AudioMuse", "Smart Fade transition GET " + dbg::scrubAuth(url));
    const HttpResult r = http.getJson(url, s.token, kTransitionTimeoutMs);
    if (!r.error.ok()) {
        outError = errorMessage(r);
        return false;
    }
    return parseTransition(r.body, fromId, toId, out, outError);
}

inline void applyFadePoint(BrowserNode& node, const FadePoint& p) {
    node.clipStart = 0.0;
    node.clipEnd = 0.0;
    node.smartFade = p.timed;
    if (!p.timed) return;
    const double length = p.duration > 0.0 ? p.duration : node.duration;
    if (p.fadeIn > 0.0 && (length <= 0.0 || p.fadeIn < length)) node.clipStart = p.fadeIn;
    if (length > 0.0 && p.fadeOut > node.clipStart && p.fadeOut < length - 0.5) node.clipEnd = p.fadeOut;
}

inline std::vector<Track> fadeTracks(const std::vector<FadePoint>& points) {
    std::vector<Track> out;
    out.reserve(points.size());
    for (const auto& p : points) {
        Track t;
        t.id = p.id;
        out.push_back(std::move(t));
    }
    return out;
}

inline void applyFadePoints(std::vector<BrowserNodePtr>& nodes, const std::vector<FadePoint>& points) {
    for (auto& n : nodes) {
        if (!n) continue;
        for (const auto& p : points)
            if (p.id == n->id) { applyFadePoint(*n, p); break; }
    }
}

inline std::vector<std::string> smartFadeIdsFromNodes(const std::vector<BrowserNodePtr>& nodes,
                                                      std::string& label) {
    std::vector<std::string> ids;
    label.clear();
    for (const auto& n : nodes) {
        if (!n || n->id.empty() || n->type != BrowserNode::Song) continue;
        if (ids.empty()) label = n->displayName;
        ids.push_back(n->id);
    }
    if (ids.size() > 1) label += trf(" + {0} more", { std::to_string(ids.size() - 1) });
    return ids;
}

inline std::string noSimilarMessage(MixFallback fallback, const std::string& audioMuseError) {
    std::string msg = tr("Navidrome has no similar songs for this one.");
    switch (fallback) {
        case MixFallback::NotConfigured:
            return msg + tr(" Navidrome answers Instant Mix from its agents: the AudioMuse-AI Navidrome "
                   "plugin when it is installed and listed in Navidrome's Agents setting, otherwise "
                   "last.fm, which knows little about less-played tracks.\n\nSet the AudioMuse-AI "
                   "server URL (Preferences > Tools > Navidrome > AudioMuse-AI) and Instant Mix asks "
                   "AudioMuse-AI directly when Navidrome has nothing.");
        case MixFallback::Unsupported:
            return msg + tr(" AudioMuse-AI can only mix from a song or an artist: start the Instant Mix "
                   "from one of this album's songs or from its artist.");
        case MixFallback::Failed:
            return msg + tr(" Asking AudioMuse-AI directly failed too.") + "\n\n" + audioMuseError;
        case MixFallback::Empty:
            return tr("Neither Navidrome nor AudioMuse-AI has similar songs for this one.");
    }
    return msg;
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
    if (seeds.size() > 1) label += trf(" + {0} more", { std::to_string(seeds.size() - 1) });
    return seeds;
}

inline std::string playlistName(Kind kind, const std::string& subject) {
    const std::string prefix = kind == Kind::Alchemy   ? "AudioMuse Alchemy"
                             : kind == Kind::SmartFade ? "AudioMuse Smart Fade" : "AudioMuse";
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
