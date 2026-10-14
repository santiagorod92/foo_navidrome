#include "TestHarness.h"
#include "../src/core/NavidromeBrowserModel.h"
#include "../src/core/SubsonicCore.h"
#include "FakeBrowserClient.h"

#include <string>
#include <vector>

namespace {

struct RouteTransport : navidrome::IHttpTransport {
    std::vector<std::string> urls;
    std::vector<std::pair<std::string, std::string>> routes;

    navidrome::HttpResult getOnce(const std::string& url) override {
        urls.push_back(url);
        navidrome::HttpResult r;
        for (const auto& kv : routes)
            if (url.find(kv.first) != std::string::npos) { r.body = kv.second; return r; }
        r.body = R"({"subsonic-response":{"status":"ok","version":"1.16.1"}})";
        return r;
    }
    void sleepMs(int) override {}
    int  jitterMs() override { return 0; }
};

struct StaticSettings : navidrome::ISettingsProvider {
    navidrome::SubsonicSettings s;
    StaticSettings() {
        s.serverUrl = "http://h";
        s.username  = "u";
        s.password  = "p";
        s.salt      = "s";
    }
    navidrome::SubsonicSettings load() const override { return s; }
};

const char* kOpenSubsonicPing =
    R"({"subsonic-response":{"status":"ok","version":"1.16.1","openSubsonic":true}})";

std::string extensionsBody(const char* names) {
    return std::string(R"({"subsonic-response":{"status":"ok","openSubsonicExtensions":[)") + names + "]}}";
}

std::vector<std::string> uris(std::initializer_list<const char*> ids) {
    std::vector<std::string> out;
    for (const char* id : ids)
        out.push_back(id[0] == '/' ? std::string(id) : "navidrome://track/" + std::string(id) + "?title=x");
    return out;
}

TEST_CASE(testStreamTimeOffset) {
    check(navidrome::streamTimeOffsetParam(0.0).empty() && navidrome::streamTimeOffsetParam(0.9).empty(),
          "no timeOffset below one second");
    check(navidrome::streamTimeOffsetParam(83.7) == "&timeOffset=83",
          "timeOffset is whole seconds, rounded down");
    check(!navidrome::streamMayBeTranscoded("raw", 0), "raw without a bitrate cap is never transcoded");
    check(navidrome::streamMayBeTranscoded("raw", 128) && navidrome::streamMayBeTranscoded("", 0) &&
          navidrome::streamMayBeTranscoded("opus", 0),
          "a format, a bitrate cap or the server default may transcode");

    RouteTransport tx;
    StaticSettings cfg;
    cfg.s.streamFormat = "opus";
    navidrome::SubsonicCore core(tx, cfg);
    const std::string url = core.streamURL("song1", "", 42.5);
    check(url.find("format=opus") != std::string::npos && url.find("timeOffset=42") != std::string::npos,
          "streamURL appends timeOffset after the transcode params");
    check(core.streamURL("song1").find("timeOffset") == std::string::npos,
          "no timeOffset by default");

    tx.routes = { { "ping.view", kOpenSubsonicPing },
                  { "getOpenSubsonicExtensions.view", extensionsBody(R"({"name":"transcodeOffset","versions":[1]})") } };
    check(core.canSeekByTimeOffset(), "transcodeOffset + transcoding → seek by reopening");

    StaticSettings rawCfg;
    rawCfg.s.streamFormat = "raw";
    RouteTransport rawTx;
    rawTx.routes = tx.routes;
    navidrome::SubsonicCore rawCore(rawTx, rawCfg);
    check(!rawCore.canSeekByTimeOffset() && rawTx.urls.empty(),
          "raw streams never seek by timeOffset and don't probe the server for it");

    RouteTransport noExtTx;
    noExtTx.routes = { { "ping.view", kOpenSubsonicPing },
                       { "getOpenSubsonicExtensions.view", extensionsBody(R"({"name":"songLyrics","versions":[1]})") } };
    StaticSettings opusCfg;
    opusCfg.s.streamFormat = "opus";
    navidrome::SubsonicCore noExtCore(noExtTx, opusCfg);
    check(!noExtCore.canSeekByTimeOffset(), "a server without transcodeOffset can't seek by reopening");
}

TEST_CASE(testPlayQueueCore) {
    RouteTransport tx;
    StaticSettings cfg;
    navidrome::SubsonicCore core(tx, cfg);
    tx.routes = { { "getPlayQueue.view", R"j({"subsonic-response":{"status":"ok","playQueue":{
        "current":"b","position":61500,"changedBy":"Symfonium",
        "entry":[{"id":"a","title":"A"},{"id":"b","title":"B","replayGain":{"trackGain":-4}}]}}})j" } };
    navidrome::PlayQueue q;
    std::string err;
    check(core.getPlayQueue(q, err) && err.empty(), "getPlayQueue succeeds");
    check(q.entries.size() == 2 && q.currentId == "b" && q.positionMs == 61500 && q.changedBy == "Symfonium",
          "entries, current, position and changedBy are parsed");
    check(q.entries[1].tags.replayGain.hasTrackGain, "queue entries carry their tags");

    tx.routes = { { "getPlayQueue.view", R"({"subsonic-response":{"status":"ok"}})" } };
    check(core.getPlayQueue(q, err) && q.entries.empty(), "no saved queue parses as empty");

    tx.urls.clear();
    check(core.savePlayQueue({ "a", "b c" }, "b c", 12345.9, err), "savePlayQueue succeeds");
    check(tx.urls.size() == 1, "savePlayQueue is one request");
    const std::string& u = tx.urls[0];
    check(u.find("savePlayQueue.view") != std::string::npos && u.find("id=a&id=b%20c") != std::string::npos &&
          u.find("current=b%20c") != std::string::npos && u.find("position=12345") != std::string::npos,
          "ids are repeated, current and position (ms) are sent");

    tx.urls.clear();
    check(!core.savePlayQueue({}, "", 0, err) && !err.empty() && tx.urls.empty(),
          "an empty queue is refused locally (the server would clear the saved one)");
}

TEST_CASE(testPlayQueueWindow) {
    using navidrome::playQueueWindow;
    auto w = playQueueWindow(10, 3, 100);
    check(w.begin == 0 && w.end == 10, "a short queue is kept whole");
    w = playQueueWindow(300, 50, 100);
    check(w.begin == 50 && w.end == 150, "a long queue keeps the current track and what follows");
    w = playQueueWindow(300, 290, 100);
    check(w.begin == 200 && w.end == 300, "near the end it backfills earlier tracks");
    w = playQueueWindow(0, 0, 100);
    check(w.begin == 0 && w.end == 0, "empty in, empty out");
    w = playQueueWindow(300, 999, 100);
    check(w.begin == 0 && w.end == 100, "an out-of-range current falls back to the start");
}

TEST_CASE(testPlayQueuePlans) {
    auto save = navidrome::planPlayQueueSave(uris({ "a", "/local/file.flac", "b", "c" }), 2, 30.5, 100);
    check(save.songIds == std::vector<std::string>({ "a", "b", "c" }), "foreign items are skipped");
    check(save.currentId == "b" && save.positionMs == 30500 && save.skippedForeign == 1 && save.truncated == 0,
          "current index maps onto the filtered list; position is sent in ms");

    save = navidrome::planPlayQueueSave(uris({ "a", "/x.mp3", "b" }), 1, 10, 100);
    check(save.currentId.empty() && save.positionMs == 0 && save.songIds.size() == 2,
          "a foreign current item saves no current/position");

    save = navidrome::planPlayQueueSave(uris({ "a", "b", "c", "d", "e" }), 3, 0, 2);
    check(save.songIds == std::vector<std::string>({ "d", "e" }) && save.truncated == 3 && save.currentId == "d",
          "the window keeps the current track");
    const std::string summary = navidrome::playQueueSaveSummary(save);
    check(summary.find("Saved 2 tracks") != std::string::npos && summary.find("3 left out") != std::string::npos,
          "the summary reports what was left out");

    save = navidrome::planPlayQueueSave(uris({ "a" }), navidrome::kNoCurrentTrack, 5, 100);
    check(save.songIds.size() == 1 && save.currentId.empty(), "no current track is fine");

    navidrome::PlayQueue q;
    navidrome::Song a; a.id = "a";
    navidrome::Song b; b.id = "b";
    navidrome::Song blank;
    q.entries = { a, blank, b };
    q.currentId = "b";
    q.positionMs = 2500;
    auto restore = navidrome::planPlayQueueRestore(q);
    check(restore.nodes.size() == 2 && restore.startIndex == 1 && restore.positionSeconds == 2.5,
          "restore starts at the current song and resumes its position");

    q.currentId = "gone";
    restore = navidrome::planPlayQueueRestore(q);
    check(restore.startIndex == 0 && restore.positionSeconds == 0,
          "an unknown current id starts from the top without seeking");

    FakeBrowserClient fc;
    std::string err;
    check(fc.savePlayQueue({ "a" }, "a", 1000, err) && fc.savedCurrent == "a" && fc.savedPositionMs == 1000,
          "the browser client seam forwards a save");
    fc.queue = q;
    navidrome::PlayQueue got;
    check(fc.getPlayQueue(got, err) && got.entries.size() == 3, "the browser client seam returns the queue");
}
}
