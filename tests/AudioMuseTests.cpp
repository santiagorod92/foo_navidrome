#include "TestHarness.h"
#include "../src/core/AudioMuse.h"
#include "FakeBrowserClient.h"

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

    navidrome::HttpResult postJson(const std::string& u, const std::string& b,
                                   const std::string& t, int timeout) override {
        ++calls; url = u; body = b; token = t; timeoutMs = timeout;
        return answer;
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
}
