#include "TestHarness.h"
#include "../src/core/AudioMuse.h"
#include "FakeBrowserClient.h"

#include <cmath>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

namespace {

namespace am = navidrome::audiomuse;

struct FakePoster : am::IJsonPoster {
    std::string url, body, token;
    int timeoutMs = 0;
    int calls = 0;
    navidrome::HttpResult answer;
    std::vector<navidrome::HttpResult> queued;
    std::vector<std::string> urls, bodies;

    navidrome::HttpResult postJson(const std::string& u, const std::string& b,
                                   const std::string& t, int timeout) override {
        ++calls; url = u; body = b; token = t; timeoutMs = timeout;
        urls.push_back(u); bodies.push_back(b);
        if (static_cast<std::size_t>(calls) <= queued.size()) return queued[calls - 1];
        return answer;
    }

    navidrome::HttpResult getJson(const std::string& u, const std::string& t, int timeout) override {
        return postJson(u, std::string(), t, timeout);
    }
};

am::Settings configured() {
    am::Settings s;
    s.url = "http://am.local:8000/";
    s.token = "tok";
    s.count = 25;
    return s;
}

TEST_CASE(testAudioMuseBodies) {
    check(am::jsonQuote("a\"b\\c\nd") == "\"a\\\"b\\\\c\\nd\"", "jsonQuote escapes quote, backslash, newline");
    check(am::jsonQuote(std::string("x\x01y")) == "\"x\\u0001y\"", "jsonQuote \\u-escapes control chars");
    check(am::jsonQuote("caf\xC3\xA9") == "\"caf\xC3\xA9\"", "jsonQuote passes UTF-8 through");

    check(am::endpointURL("http://h:8000///", "/api/x") == "http://h:8000/api/x", "endpoint strips trailing slashes");
    check(am::endpointURL("", "/api/x").empty(), "no base, no endpoint");

    check(am::textSearchBody("rainy jazz", 30, "") == "{\"query\":\"rainy jazz\",\"limit\":30}",
          "text search body");
    check(am::instantPlaylistBody("road trip", 0, "Navidrome") ==
          "{\"userInput\":\"road trip\",\"n\":50,\"server\":\"Navidrome\"}",
          "instant playlist body, default count, server name");
    check(am::clampCount(9999) == am::kMaxCount && am::clampCount(-3) == am::kDefaultCount,
          "count clamped");

    std::vector<am::AlchemySeed> seeds = { {"s1", false, false}, {"ar1", true, false},
                                           {"s2", false, true}, {"", false, false} };
    check(am::alchemyBody(seeds, 10, "") ==
          "{\"items\":[{\"id\":\"s1\",\"op\":\"ADD\",\"type\":\"song\"},"
          "{\"id\":\"ar1\",\"op\":\"ADD\",\"type\":\"artist\"},"
          "{\"id\":\"s2\",\"op\":\"SUBTRACT\",\"type\":\"song\"}],\"n\":10}",
          "alchemy body: songs, artists, subtract, empty id skipped");
}

TEST_CASE(testAudioMuseParse) {
    std::string err;
    auto clap = am::parseTracks(
        "{\"query\":\"q\",\"count\":2,\"results\":["
        "{\"item_id\":\"a1\",\"title\":\"One\",\"author\":\"X\",\"similarity\":0.9},"
        "{\"title\":\"no id\"},"
        "{\"item_id\":42,\"title\":\"Two\",\"author\":\"Y\",\"album\":\"Al\"}]}",
        am::Kind::TextSearch, err);
    check(err.empty() && clap.size() == 2, "text search: rows without an id dropped");
    check(clap.size() == 2 && clap[0].id == "a1" && clap[0].artist == "X" &&
          clap[1].id == "42" && clap[1].album == "Al", "text search: author -> artist, numeric id");

    auto chat = am::parseTracks(
        "{\"response\":{\"message\":\"log\",\"query_results\":["
        "{\"item_id\":\"c1\",\"title\":\"T\",\"artist\":\"A\"}]}}",
        am::Kind::InstantPlaylist, err);
    check(err.empty() && chat.size() == 1 && chat[0].artist == "A", "instant playlist: response.query_results");

    am::parseTracks("{\"response\":{\"message\":\"step 1\\nNo AI provider configured\\n\","
                    "\"query_results\":null}}", am::Kind::InstantPlaylist, err);
    check(err == "AudioMuse-AI found no songs: No AI provider configured",
          "instant playlist: empty result reports the log's last line");

    auto alch = am::parseTracks("{\"results\":[{\"item_id\":\"z\",\"title\":\"Z\",\"author\":\"W\"}],"
                                "\"filtered_out\":[]}", am::Kind::Alchemy, err);
    check(err.empty() && alch.size() == 1 && alch[0].id == "z", "alchemy: results[]");

    am::parseTracks("<html>", am::Kind::Alchemy, err);
    check(!err.empty(), "non-JSON body is an error");

    navidrome::HttpResult r;
    r.error = { navidrome::ErrorKind::Unknown, 400, 0, "HTTP 400" };
    r.body = "{\"error\":\"CLAP text search is disabled\",\"error_message\":\"CLAP disabled\"}";
    check(am::errorMessage(r) == "CLAP disabled", "error_message preferred");
    r.body = "{\"error\":\"legacy only\"}";
    check(am::errorMessage(r) == "legacy only", "error fallback");
    r.body = "";
    check(am::errorMessage(r) == "HTTP 400", "transport message when body has none");
}

TEST_CASE(testAudioMuseRequests) {
    FakePoster http;
    std::string err;

    auto none = am::textSearch(http, am::Settings{}, "q", err);
    check(none.empty() && !err.empty() && http.calls == 0, "unconfigured: no request, error set");

    http.answer.body = "{\"results\":[{\"item_id\":\"a\"}]}";
    auto t = am::textSearch(http, configured(), "chill", err);
    check(err.empty() && t.size() == 1, "text search ok");
    check(http.url == "http://am.local:8000/api/clap/search" && http.token == "tok" &&
          http.timeoutMs == am::kSearchTimeoutMs, "text search endpoint, bearer token, timeout");
    check(http.body == "{\"query\":\"chill\",\"limit\":25}", "settings count used");

    http.answer.body = "{\"response\":{\"query_results\":[{\"item_id\":\"b\"}]}}";
    am::instantPlaylist(http, configured(), "x", err);
    check(http.url == "http://am.local:8000/chat/api/chatPlaylist" &&
          http.timeoutMs == am::kPlaylistTimeoutMs, "instant playlist under /chat, long timeout");

    const int before = http.calls;
    am::alchemy(http, configured(), { {"s", false, true} }, err);
    check(!err.empty() && http.calls == before, "alchemy with only SUBTRACT seeds isn't sent");
    http.answer.body = "{\"results\":[]}";
    am::alchemy(http, configured(), { {"s", false, false} }, err);
    check(err.empty() && http.url == "http://am.local:8000/api/alchemy", "alchemy endpoint");

    http.answer.error = { navidrome::ErrorKind::ServerError, 503, 0, "HTTP 503" };
    http.answer.body = "{\"error\":\"CLAP cache not loaded\"}";
    auto failed = am::textSearch(http, configured(), "q", err);
    check(failed.empty() && err == "Text Search: CLAP cache not loaded", "HTTP error surfaces AudioMuse's text");
}

TEST_CASE(testAudioMuseResolve) {
    FakeBrowserClient client;
    client.failIds = { "gone" };
    std::vector<am::Track> tracks = { {"a", "A", "", ""}, {"gone", "G", "", ""}, {"b", "B", "", ""} };
    std::size_t unresolved = 0;
    auto nodes = am::resolveTracks(client, tracks, unresolved);
    check(nodes.size() == 2 && unresolved == 1, "missing song skipped and counted");
    check(nodes.size() == 2 && nodes[0]->id == "a" && nodes[1]->id == "b" &&
          nodes[0]->suffix == "flac" && nodes[0]->type == navidrome::BrowserNode::Song,
          "order kept, full song metadata from getSong");

    using navidrome::BrowserNode;
    std::vector<navidrome::BrowserNodePtr> sel;
    for (auto [type, id, name] : { std::tuple{BrowserNode::Song, "s1", "First"},
                                   std::tuple{BrowserNode::Album, "al", "Album"},
                                   std::tuple{BrowserNode::Artist, "ar", "Band"} }) {
        auto n = std::make_shared<BrowserNode>();
        n->type = type; n->id = id; n->displayName = name;
        sel.push_back(n);
    }
    std::string label;
    auto seeds = am::seedsFromNodes(sel, label);
    check(seeds.size() == 2 && !seeds[0].artist && seeds[1].artist && seeds[1].id == "ar",
          "alchemy seeds: songs + artists, albums skipped");
    check(label == "First + 1 more", "alchemy label");

    check(am::playlistName(am::Kind::TextSearch, "rainy") == "AudioMuse: rainy", "playlist name");
    check(am::playlistName(am::Kind::Alchemy, "") == "AudioMuse Alchemy", "alchemy playlist name");
    const std::string longName = am::playlistName(am::Kind::TextSearch,
        std::string(56, 'x') + "\xC3\xA9\xC3\xA9\xC3\xA9");
    check(longName == "AudioMuse: " + std::string(56, 'x') + "...", "long name cut on a UTF-8 boundary");
}

TEST_CASE(testAudioMuseInstantMixFallback) {
    FakePoster http;
    std::string err;

    check(am::canMixFrom(navidrome::BrowserNode::Song) && am::canMixFrom(navidrome::BrowserNode::Artist) &&
          !am::canMixFrom(navidrome::BrowserNode::Album), "mix seeds: song and artist, not album");

    auto none = am::similarTo(http, configured(), "al1", navidrome::BrowserNode::Album, err);
    check(none.empty() && !err.empty() && http.calls == 0, "album seed isn't sent");
    am::similarTo(http, am::Settings{}, "s1", navidrome::BrowserNode::Song, err);
    check(!err.empty() && http.calls == 0, "unconfigured: no request");

    http.answer.body = "{\"results\":[{\"item_id\":\"x\"},{\"item_id\":\"y\"}]}";
    auto songs = am::similarTo(http, configured(), "s1", navidrome::BrowserNode::Song, err);
    check(err.empty() && songs.size() == 2, "song seed answered");
    check(http.url == "http://am.local:8000/api/alchemy" && http.timeoutMs == am::kSearchTimeoutMs &&
          http.body == "{\"items\":[{\"id\":\"s1\",\"op\":\"ADD\",\"type\":\"song\"}],\"n\":25}",
          "song seed = one-item alchemy, settings count");
    am::similarTo(http, configured(), "ar1", navidrome::BrowserNode::Artist, err);
    check(http.body.find("\"type\":\"artist\"") != std::string::npos, "artist seed typed artist");

    http.answer.error = { navidrome::ErrorKind::Network, 0, 0, "connection refused" };
    http.answer.body.clear();
    am::similarTo(http, configured(), "s1", navidrome::BrowserNode::Song, err);
    check(err == "Instant Mix: connection refused", "failure names Instant Mix, not Song Alchemy");

    const std::string notConf = am::noSimilarMessage(am::MixFallback::NotConfigured, {});
    check(notConf.find("last.fm") != std::string::npos &&
          notConf.find("Preferences > Tools > Navidrome > AudioMuse-AI") != std::string::npos,
          "not configured: explains the agents and points at the AudioMuse prefs");
    check(am::noSimilarMessage(am::MixFallback::Failed, err).find(err) != std::string::npos,
          "failed: quotes the AudioMuse error");
    check(am::noSimilarMessage(am::MixFallback::Empty, {}).find("Neither") == 0, "empty on both sides");
    check(am::noSimilarMessage(am::MixFallback::Unsupported, {}).find("artist") != std::string::npos,
          "album: suggests a song or the artist");
}

navidrome::HttpResult okBody(const std::string& body) {
    navidrome::HttpResult r;
    r.body = body;
    return r;
}

TEST_CASE(testAudioMuseSmartFade) {
    check(am::byTrackBody("s1", 25, "") == "{\"item_id\":\"s1\",\"n_results\":25}", "by_track body");
    check(am::smartFadeBody({ "a", "", "b" }, "nd") == "{\"item_ids\":[\"a\",\"b\"],\"server\":\"nd\"}",
          "smart_fade body skips blank ids, carries the server");

    FakePoster http;
    std::string err;
    http.queued = {
        okBody("{\"item_id\":\"seed\",\"results\":[{\"item_id\":\"x\"},{\"item_id\":\"seed\"},{\"item_id\":\"y\"}]}"),
        okBody("{\"count\":3,\"results\":["
               "{\"item_id\":\"seed\",\"fade_in_seconds\":0.0,\"fade_out_seconds\":280.5,\"duration_seconds\":281.5},"
               "{\"item_id\":\"y\",\"fade_in_seconds\":4.0,\"fade_out_seconds\":115.0,\"duration_seconds\":130.0},"
               "{\"item_id\":\"x\",\"fade_in_seconds\":null,\"fade_out_seconds\":null,\"duration_seconds\":200.0}]}"),
    };
    auto points = am::smartFade(http, configured(), { "seed" }, err);
    check(err.empty() && http.calls == 2, "one seed: by_track then smart_fade");
    check(http.urls.size() == 2 && http.urls[0] == "http://am.local:8000/api/recording_search/by_track" &&
          http.urls[1] == "http://am.local:8000/api/recording_search/smart_fade", "recording search endpoints");
    check(http.bodies.size() == 2 && http.bodies[1] == "{\"item_ids\":[\"seed\",\"x\",\"y\"]}",
          "seed first, echoed seed dropped from the results");
    check(points.size() == 3 && points[0].id == "seed" && points[1].id == "y" && points[1].timed &&
          points[1].fadeIn == 4.0 && points[1].fadeOut == 115.0 && !points[2].timed,
          "server order and fade times kept, null times untimed");

    FakePoster many;
    many.answer = okBody("{\"results\":[{\"item_id\":\"b\"},{\"item_id\":\"a\"}]}");
    auto ordered = am::smartFade(many, configured(), { "a", "b" }, err);
    check(err.empty() && many.calls == 1 && many.url == "http://am.local:8000/api/recording_search/smart_fade" &&
          ordered.size() == 2 && ordered[0].id == "b", "several songs: smart_fade only");

    FakePoster lonely;
    lonely.answer = okBody("{\"results\":[]}");
    am::smartFade(lonely, configured(), { "seed" }, err);
    check(!err.empty() && lonely.calls == 1, "no similar recordings: smart_fade isn't sent");

    using navidrome::BrowserNode;
    auto node = [](const char* id, double duration) {
        auto n = std::make_shared<BrowserNode>();
        n->type = BrowserNode::Song; n->id = id; n->duration = duration;
        return n;
    };
    std::vector<navidrome::BrowserNodePtr> nodes = { node("seed", 281), node("y", 130), node("x", 200) };
    am::applyFadePoints(nodes, points);
    check(nodes[0]->clipStart == 0.0 && nodes[0]->clipEnd == 280.5, "first song starts at 0, cut at its fade out");
    check(nodes[1]->clipStart == 4.0 && nodes[1]->clipEnd == 115.0, "middle song cut at both points");
    check(nodes[2]->clipStart == 0.0 && nodes[2]->clipEnd == 0.0, "untimed song plays whole");

    am::FadePoint last;
    last.id = "z"; last.timed = true; last.fadeIn = 10.0; last.fadeOut = 239.8; last.duration = 240.0;
    auto z = node("z", 240);
    am::applyFadePoint(*z, last);
    check(z->clipStart == 10.0 && z->clipEnd == 0.0, "fade out at the very end leaves the end alone");

    std::string label;
    std::vector<navidrome::BrowserNodePtr> sel = { node("s1", 1), node("s2", 1) };
    sel[0]->displayName = "First";
    auto album = std::make_shared<BrowserNode>();
    album->type = BrowserNode::Album; album->id = "al";
    sel.push_back(album);
    auto ids = am::smartFadeIdsFromNodes(sel, label);
    check(ids.size() == 2 && ids[1] == "s2" && label == "First + 1 more", "smart fade takes songs only");
    check(am::playlistName(am::Kind::SmartFade, "First") == "AudioMuse Smart Fade: First", "smart fade playlist name");
}

TEST_CASE(testAudioMuseTransition) {
    const am::Settings s = configured();
    check(am::transitionURL(s, "a b", "c", 216.5, 21.0) ==
          "http://am.local:8000/api/recording_search/transition?from_id=a%20b&to_id=c&out=216.5&in=21",
          "transition URL carries both ids and points");

    FakePoster http;
    http.answer.body = "{\"mix_at\":213.7788,\"mix_seconds\":2.6222,\"in_at\":21.1495,\"resume_at\":23.7397,"
                       "\"stretch\":0.9878,\"tempo_from\":183.1,\"tempo_to\":92.7}";
    navidrome::smartfade::Transition t;
    std::string err;
    check(am::transition(http, s, "a", "b", 216.5, 21.0, t, err) && err.empty(), "transition parsed");
    check(http.token == "tok" && http.timeoutMs == am::kTransitionTimeoutMs, "transition sends the token, long timeout");
    check(t.fromId == "a" && t.toId == "b" && t.mixAt == 213.7788 && t.mixSeconds == 2.6222 &&
          t.inAt == 21.1495 && t.resumeAt == 23.7397 && t.stretch == 0.9878, "transition fields");
    check(std::abs(t.mixEnd() - 216.401) < 1e-9, "mix ends at mix_at + mix_seconds");

    http.answer.body = "{\"mix_at\":1,\"mix_seconds\":30,\"in_at\":1,\"resume_at\":31}";
    check(!am::transition(http, s, "a", "b", 1, 1, t, err) && !err.empty(), "a mix past the limit is refused");
    http.answer.body = "{\"mix_at\":1}";
    check(!am::transition(http, s, "a", "b", 1, 1, t, err) && !err.empty(), "missing fields refused");
    http.answer.error = { navidrome::ErrorKind::Unknown, 400, 0, "HTTP 400" };
    http.answer.body = "{\"error\":\"No steady beat to match.\"}";
    check(!am::transition(http, s, "a", "b", 1, 1, t, err) && !err.empty(), "no steady beat is an error");
    check(!am::transition(http, am::Settings{}, "a", "b", 1, 1, t, err), "unconfigured refused");

    navidrome::BrowserNode n;
    n.duration = 130.0;
    am::FadePoint p;
    p.id = "x"; p.timed = true; p.fadeIn = 4.0; p.fadeOut = 115.0; p.duration = 130.0;
    am::applyFadePoint(n, p);
    check(n.smartFade && navidrome::trackURIFromNode(n).smartFade, "a timed point marks the track for the mix");
    p.timed = false;
    am::applyFadePoint(n, p);
    check(!n.smartFade, "an untimed song isn't mixed");
}
}
