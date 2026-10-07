// Unit tests: lyrics — the LRC / structuredLyrics parsers and active-line lookup
// (SubsonicTypes.h), SubsonicCore::getLyrics's by-id → legacy fallback, and the
// cached lyricsForTrackURI() behind the macOS panel and navidrome_lyrics_api.
#include "TestHarness.h"
#include "../src/core/SubsonicCore.h"
#include "../src/core/NavidromeBrowserModel.h"
#include "FakeBrowserClient.h"

#include <string>
#include <vector>

namespace {

navidrome::json::Value parseJson(const std::string& body) {
    std::string err;
    auto v = navidrome::json::parse(body, err);
    return v;
}

TEST_CASE(testLrcParsing) {
    using navidrome::parseLyricsText;
    long long ms = 0;
    check(navidrome::parseLrcTimeTag("[01:02.34]x", ms) == 10 && ms == 62340, "[mm:ss.xx] tag");
    check(navidrome::parseLrcTimeTag("[1:02]x", ms) == 6 && ms == 62000, "[m:ss] tag");
    check(navidrome::parseLrcTimeTag("[00:01.5]", ms) == 9 && ms == 1500, "one fractional digit = tenths");
    check(navidrome::parseLrcTimeTag("[00:01.123]", ms) == 11 && ms == 1123, "ms precision");
    check(navidrome::parseLrcTimeTag("[ar:Someone]", ms) == 0, "metadata tag isn't a time tag");
    check(navidrome::parseLrcTimeTag("plain", ms) == 0, "plain text isn't a time tag");

    auto plain = parseLyricsText("Line one\r\nLine two\n\nLine four\n\n");
    check(!plain.synced && plain.lines.size() == 4, "plain text: one line each, trailing blanks dropped");
    check(plain.lines.size() == 4 && plain.lines[1].text == "Line two" && plain.lines[2].text.empty() &&
          plain.lines[0].startMs == -1, "CR stripped, inner blank kept, no timing");

    auto lrc = parseLyricsText("[ar:Someone]\n[00:20.00]Second\n[00:10.00][00:30.00]Chorus\n");
    check(lrc.synced && lrc.lines.size() == 3, "LRC: metadata dropped, repeated tag emits twice");
    check(lrc.lines.size() == 3 && lrc.lines[0].startMs == 10000 && lrc.lines[0].text == "Chorus" &&
          lrc.lines[1].text == "Second" && lrc.lines[2].startMs == 30000, "LRC lines sorted by time");

    check(parseLyricsText("").empty(), "empty text = no lyrics");
}

TEST_CASE(testStructuredLyricsParsing) {
    auto root = parseJson(R"({"lyricsList":{"structuredLyrics":[
        {"lang":"eng","synced":false,"line":[{"value":"plain a"},{"value":"plain b"}]},
        {"lang":"eng","synced":true,"offset":500,
         "line":[{"start":2500,"value":"two"},{"start":1000,"value":"one"},{"value":"no start"}]},
        {"lang":"xxx","synced":true,"line":[]}
    ]}})");
    auto all = navidrome::parseLyricsList(root);
    check(all.size() == 2, "empty lyric set dropped");
    check(!all.empty() && all[0].synced, "synced set ordered first");
    check(!all.empty() && all[0].lines.size() == 2, "synced line without a start skipped");
    check(!all.empty() && all[0].lines[0].startMs == 500 && all[0].lines[0].text == "one" &&
          all[0].lines[1].startMs == 2000, "offset subtracted, lines sorted by start");
    check(all.size() == 2 && !all[1].synced && all[1].lines[1].text == "plain b" &&
          all[1].lines[1].startMs == -1, "plain set kept with no timing");

    // Single structuredLyrics object collapsed from a one-element array.
    auto one = navidrome::parseLyricsList(parseJson(
        R"({"lyricsList":{"structuredLyrics":{"synced":true,"line":{"start":0,"value":"solo"}}}})"));
    check(one.size() == 1 && one[0].lines.size() == 1 && one[0].lines[0].text == "solo",
          "collapsed single entry/line parsed");
    check(navidrome::parseLyricsList(parseJson(R"({"lyricsList":{}})")).empty(), "no lyrics");

    auto legacy = navidrome::parseLegacyLyrics(parseJson(
        R"({"lyrics":{"artist":"A","title":"T","value":"[00:01.00]hi\n[00:02.00]there"}})"));
    check(legacy.synced && legacy.lines.size() == 2, "legacy value carrying LRC is synced");
    check(navidrome::parseLegacyLyrics(parseJson(R"({"lyrics":{}})")).empty(), "legacy: no value");
}

TEST_CASE(testActiveLyricLine) {
    navidrome::Lyrics l;
    l.synced = true;
    l.lines = { {1000, "a"}, {2000, "b"}, {2000, "b2"}, {5000, "c"} };
    check(navidrome::activeLyricLine(l, 0) == -1, "before the first line: none");
    check(navidrome::activeLyricLine(l, 1000) == 0, "exactly at a start");
    check(navidrome::activeLyricLine(l, 2500) == 2, "same-start lines: the last one wins");
    check(navidrome::activeLyricLine(l, 99999) == 3, "past the end: last line");
    l.synced = false;
    check(navidrome::activeLyricLine(l, 2500) == -1, "unsynced: never active");
}

TEST_CASE(testLyricsCache) {
    navidrome::LyricsCache c(2);
    navidrome::Lyrics a; a.lines = { {-1, "a"} };
    navidrome::Lyrics out;
    check(!c.get("1", out), "miss on empty cache");
    c.put("1", a);
    c.put("2", navidrome::Lyrics{});
    check(c.get("1", out) && out.lines.size() == 1, "hit");
    check(c.get("2", out) && out.empty(), "'no lyrics' is cached too");
    c.put("3", a);
    check(!c.get("1", out) && c.get("3", out), "oldest evicted at capacity");
}

// --- SubsonicCore::getLyrics ------------------------------------------------
struct LyricsTransport : navidrome::IHttpTransport {
    std::vector<std::string> urls;
    std::vector<std::pair<std::string, std::string>> routes;
    int notFoundById = 0;   // answer getLyricsBySongId with HTTP 404 this many times

    navidrome::HttpResult getOnce(const std::string& url) override {
        urls.push_back(url);
        navidrome::HttpResult r;
        if (notFoundById > 0 && url.find("getLyricsBySongId.view") != std::string::npos) {
            --notFoundById;
            r.error = { navidrome::ErrorKind::NotFound, 404, 0, "HTTP 404" };
            return r;
        }
        for (const auto& kv : routes)
            if (url.find(kv.first) != std::string::npos) { r.body = kv.second; return r; }
        r.body = R"({"subsonic-response":{"status":"ok","version":"1.16.1"}})";
        return r;
    }
    void sleepMs(int) override {}
    int  jitterMs() override { return 0; }
};

struct LyricsSettings : navidrome::ISettingsProvider {
    navidrome::SubsonicSettings s;
    LyricsSettings() { s.serverUrl = "http://h"; s.username = "u"; s.password = "p"; s.salt = "s"; }
    navidrome::SubsonicSettings load() const override { return s; }
};

size_t countUrls(const LyricsTransport& tx, const char* needle) {
    size_t n = 0;
    for (const auto& u : tx.urls) if (u.find(needle) != std::string::npos) ++n;
    return n;
}

TEST_CASE(testSubsonicCoreLyrics) {
    const std::string byId = R"({"subsonic-response":{"status":"ok","lyricsList":{"structuredLyrics":[
        {"synced":true,"line":[{"start":0,"value":"synced line"}]}]}}})";
    const std::string legacy = R"({"subsonic-response":{"status":"ok","lyrics":{"value":"plain line"}}})";

    // By-id hit: synced lyrics, no legacy request.
    {
        LyricsTransport tx; LyricsSettings cfg;
        tx.routes = { {"getLyricsBySongId.view", byId}, {"getLyrics.view", legacy} };
        navidrome::SubsonicCore core(tx, cfg);
        std::string err;
        auto l = core.getLyrics("song 1", "A", "T", err);
        check(err.empty() && l.synced && l.lines.size() == 1 && l.lines[0].text == "synced line",
              "by-id lyrics returned");
        check(tx.urls.size() == 1 && tx.urls[0].find("id=song%201") != std::string::npos,
              "one by-id request, id percent-encoded");
    }
    // HTTP 404 on by-id: fall back to artist/title, and stop probing by-id on this server.
    {
        LyricsTransport tx; LyricsSettings cfg;
        tx.routes = { {"getLyrics.view", legacy} };
        tx.notFoundById = 1;
        navidrome::SubsonicCore core(tx, cfg);
        std::string err;
        auto l = core.getLyrics("s1", "Art ist", "Ti&tle", err);
        check(err.empty() && !l.synced && l.lines.size() == 1 && l.lines[0].text == "plain line",
              "legacy fallback after 404");
        check(countUrls(tx, "artist=Art%20ist&title=Ti%26tle") == 1, "legacy query encoded");
        core.getLyrics("s2", "A", "T", err);
        check(countUrls(tx, "getLyricsBySongId.view") == 1, "404 remembered — no second by-id probe");
        cfg.s.serverUrl = "http://other";
        core.getLyrics("s3", "A", "T", err);
        check(countUrls(tx, "getLyricsBySongId.view") == 2, "a different server is probed again");
    }
    // Any other by-id failure is an error, not a fallback.
    {
        LyricsTransport tx; LyricsSettings cfg;
        tx.routes = { {"getLyricsBySongId.view",
                       R"({"subsonic-response":{"status":"failed","error":{"code":70,"message":"gone"}}})"} };
        navidrome::SubsonicCore core(tx, cfg);
        std::string err;
        auto l = core.getLyrics("s1", "A", "T", err);
        check(l.empty() && err == "gone" && countUrls(tx, "getLyrics.view") == 0,
              "Subsonic error surfaced, no legacy request");
    }
    // Legacy path needs artist + title.
    {
        LyricsTransport tx; LyricsSettings cfg;
        tx.notFoundById = 1;
        navidrome::SubsonicCore core(tx, cfg);
        std::string err;
        check(core.getLyrics("s1", "", "T", err).empty() && err.empty() && tx.urls.size() == 1,
              "no artist: no legacy request, no error");
    }
}

TEST_CASE(testLyricsForTrackURI) {
    navidrome::lyricsCache().clear();
    FakeBrowserClient fc;
    fc.lyrics.lines = { {-1, "hello"} };
    std::string err;

    check(navidrome::lyricsForTrackURI(fc, "C:\\music\\a.flac", err).empty() && fc.calls.empty(),
          "non-navidrome path: no request");

    const std::string uri = "navidrome://track/t1?title=My%20Song&artist=Band";
    auto l = navidrome::lyricsForTrackURI(fc, uri, err);
    check(err.empty() && l.lines.size() == 1, "lyrics fetched");
    check(fc.calls.size() == 1 && fc.calls[0] == "getLyrics:t1:Band:My Song",
          "id + decoded artist/title passed to the client");
    navidrome::lyricsForTrackURI(fc, uri, err);
    check(fc.calls.size() == 1, "second lookup served from the cache");

    FakeBrowserClient failing;
    failing.error = "HTTP 500";
    const std::string uri2 = "navidrome://track/t2";
    l = navidrome::lyricsForTrackURI(failing, uri2, err);
    check(l.empty() && err == "HTTP 500", "failure surfaced");
    err.clear();
    navidrome::lyricsForTrackURI(failing, uri2, err);
    check(failing.calls.size() == 2, "failures aren't cached — retried");
    navidrome::lyricsCache().clear();
}

} // namespace
