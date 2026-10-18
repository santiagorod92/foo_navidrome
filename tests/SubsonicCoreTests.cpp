#include "TestHarness.h"
#include "../src/core/SubsonicCore.h"

#include <string>
#include <vector>

namespace {

struct FakeTransport : navidrome::IHttpTransport {
    std::vector<std::string> urls;
    std::vector<std::pair<std::string, std::string>> routes;
    navidrome::Error forcedError;
    int failTimes = 0;
    int authCalls = 0;

    navidrome::HttpResult getOnce(const std::string& url) override {
        urls.push_back(url);
        navidrome::HttpResult r;
        if (failTimes > 0) { --failTimes; r.error = forcedError; return r; }
        for (const auto& kv : routes)
            if (url.find(kv.first) != std::string::npos) { r.body = kv.second; return r; }
        r.body = R"({"subsonic-response":{"status":"ok","version":"1.16.1"}})";
        return r;
    }
    void onAuthRejected() override { ++authCalls; }
    void sleepMs(int) override {}
    int  jitterMs() override { return 0; }
};

struct FakeSettings : navidrome::ISettingsProvider {
    navidrome::SubsonicSettings s;
    navidrome::SubsonicSettings load() const override { return s; }
};

static navidrome::SubsonicSettings basicSettings() {
    navidrome::SubsonicSettings s;
    s.serverUrl = "http://h";
    s.username  = "u";
    s.password  = "p";
    s.salt      = "s";
    return s;
}

TEST_CASE(testSubsonicCore) {
    using navidrome::SubsonicCore;

    {
        FakeTransport tx;
        FakeSettings cfg;
        cfg.s.serverUrl = "https://nd.example.com/";
        cfg.s.username  = "alice";
        cfg.s.password  = "secret";
        cfg.s.salt      = "fb2k_navidrome";
        SubsonicCore core(tx, cfg);

        check(core.isConfigured(), "isConfigured true with url + user + pass");

        const std::string url = core.buildURL("ping.view");
        check(url.rfind("https://nd.example.com/rest/ping.view?", 0) == 0,
              "buildURL strips the trailing slash and mounts /rest/<endpoint>");
        const std::string tok =
            SubsonicCore::generateToken("secret", "fb2k_navidrome");
        check(url.find("&t=" + tok) != std::string::npos,
              "buildURL carries md5(password + salt) as the t= token");
        check(url.find("u=alice") != std::string::npos &&
              url.find("v=1.16.1") != std::string::npos &&
              url.find("f=json") != std::string::npos,
              "buildURL carries the u=/v=/f= auth params");

        const std::string s1 = core.streamURL("song1");
        check(s1.find("/rest/stream.view?") != std::string::npos &&
              s1.find("id=song1") != std::string::npos &&
              s1.find("coverArt=") == std::string::npos,
              "streamURL omits the coverArt param when none is given");
        check(core.streamURL("song1", "art9").find("coverArt=art9") != std::string::npos,
              "streamURL embeds the coverArt id when given");
    }

    {
        FakeTransport tx; FakeSettings cfg; cfg.s = basicSettings();
        tx.routes.push_back({"getArtists.view",
            R"({"subsonic-response":{"status":"ok","artists":{"index":[{"artist":[)"
            R"({"id":"ar1","name":"A"},{"id":"ar2","name":"B"}]}]}}})"});
        SubsonicCore core(tx, cfg);
        std::string err;
        auto artists = core.getArtists(err);
        check(err.empty() && artists.size() == 2 && artists[0].id == "ar1" &&
              artists[1].name == "B",
              "getArtists parses the nested index/artist arrays");
        check(tx.urls.size() == 1 &&
              tx.urls[0].find("getArtists.view") != std::string::npos,
              "getArtists issues one request when the library filter is off");
    }

    {
        FakeTransport tx; FakeSettings cfg; cfg.s = basicSettings();
        tx.forcedError = { navidrome::ErrorKind::Network, 0, 0, "reset" };
        tx.failTimes = 2;
        SubsonicCore core(tx, cfg);
        std::string err;
        check(core.ping(err) && err.empty() && tx.urls.size() == 3,
              "httpGet retries a retryable failure up to kMaxAttempts");
    }
    {
        FakeTransport tx; FakeSettings cfg; cfg.s = basicSettings();
        tx.forcedError = { navidrome::ErrorKind::NotFound, 404, 0, "nope" };
        tx.failTimes = 5;
        SubsonicCore core(tx, cfg);
        std::string err;
        check(!core.ping(err) && tx.urls.size() == 1,
              "httpGet does not retry a deterministic failure");
    }

    {
        FakeTransport tx; FakeSettings cfg; cfg.s = basicSettings();
        tx.forcedError = { navidrome::ErrorKind::Auth, 401, 0, "bad creds" };
        tx.failTimes = 1;
        SubsonicCore core(tx, cfg);
        std::string err;
        core.ping(err);
        check(tx.authCalls == 1 && core.lastError().kind == navidrome::ErrorKind::Auth,
              "a transport Auth failure calls onAuthRejected and lands in lastError");
    }
    {
        FakeTransport tx; FakeSettings cfg; cfg.s = basicSettings();
        tx.routes.push_back({"ping.view",
            R"({"subsonic-response":{"status":"failed","error":)"
            R"({"code":40,"message":"Wrong username or password"}}})"});
        SubsonicCore core(tx, cfg);
        std::string err;
        check(!core.ping(err) && tx.authCalls == 1 && !err.empty(),
              "a subsonic status=failed code 40 is classified Auth and warns once");
    }

    {
        FakeTransport tx; FakeSettings cfg; cfg.s = basicSettings();
        cfg.s.libraryFilter = true;
        cfg.s.libraryIdsCsv = "1,2";
        tx.routes.push_back({"getMusicFolders.view",
            R"({"subsonic-response":{"status":"ok","musicFolders":{"musicFolder":[)"
            R"({"id":1,"name":"A"},{"id":2,"name":"B"},{"id":3,"name":"C"}]}}})"});
        tx.routes.push_back({"getAlbumList2.view",
            R"({"subsonic-response":{"status":"ok","albumList2":{"album":[)"
            R"({"id":"al1","name":"One"}]}}})"});
        SubsonicCore core(tx, cfg);
        std::string err;
        auto albums = core.getAlbumList(navidrome::AlbumListType::Newest, 50, err);

        int listCalls = 0, mf1 = 0, mf2 = 0;
        for (const auto& u : tx.urls) {
            if (u.find("getAlbumList2.view") == std::string::npos) continue;
            ++listCalls;
            if (u.find("musicFolderId=1") != std::string::npos) ++mf1;
            if (u.find("musicFolderId=2") != std::string::npos) ++mf2;
        }
        check(listCalls == 2 && mf1 == 1 && mf2 == 1,
              "getAlbumList fans out once per selected library with musicFolderId");
        check(albums.size() == 1 && albums[0].id == "al1",
              "the fan-out merge dedupes results by id");
    }

    {
        FakeTransport tx; FakeSettings cfg; cfg.s = basicSettings();
        tx.routes.push_back({"songOffset=0",
            R"({"subsonic-response":{"status":"ok","searchResult3":{"song":[)"
            R"({"id":"s1","title":"A"},{"id":"s2","title":"B"}]}}})"});
        tx.routes.push_back({"songOffset=2",
            R"({"subsonic-response":{"status":"ok","searchResult3":{"song":[)"
            R"({"id":"s3","title":"C"}]}}})"});
        SubsonicCore core(tx, cfg);
        std::string err;
        auto songs = core.getAllSongs(err, 2);
        check(err.empty() && songs.size() == 3 && songs[2].id == "s3",
              "getAllSongs concatenates pages until a short one");
        check(tx.urls.size() == 2 &&
              tx.urls[0].find("search3.view") != std::string::npos &&
              tx.urls[0].find("query=&") != std::string::npos &&
              tx.urls[0].find("songCount=2") != std::string::npos &&
              tx.urls[0].find("artistCount=0&albumCount=0") != std::string::npos,
              "getAllSongs pages an empty search3 query with artists/albums off");
    }
    {
        FakeTransport tx; FakeSettings cfg; cfg.s = basicSettings();
        tx.routes.push_back({"search3.view",
            R"({"subsonic-response":{"status":"ok","searchResult3":{"song":[)"
            R"({"id":"s1","title":"A"},{"id":"s2","title":"B"}]}}})"});
        SubsonicCore core(tx, cfg);
        std::string err;
        auto songs = core.getAllSongs(err, 2);
        check(err.empty() && songs.size() == 2 && tx.urls.size() == 2,
              "getAllSongs stops when a page brings no new ids (offset ignored)");
    }
    {
        FakeTransport tx; FakeSettings cfg; cfg.s = basicSettings();
        tx.routes.push_back({"search3.view",
            R"({"subsonic-response":{"status":"failed","error":{"code":70,"message":"nope"}}})"});
        SubsonicCore core(tx, cfg);
        std::string err;
        auto songs = core.getAllSongs(err, 2);
        check(!err.empty() && songs.empty(), "getAllSongs surfaces a server error");
    }
    {
        FakeTransport tx; FakeSettings cfg; cfg.s = basicSettings();
        tx.routes.push_back({"offset=0",
            R"({"subsonic-response":{"status":"ok","albumList2":{"album":[)"
            R"({"id":"a1","name":"A","year":1971},{"id":"a2","name":"B","year":1975}]}}})"});
        tx.routes.push_back({"offset=2",
            R"({"subsonic-response":{"status":"ok","albumList2":{"album":[)"
            R"({"id":"a3","name":"C","year":1979}]}}})"});
        SubsonicCore core(tx, cfg);
        std::string err;
        auto albums = core.getAlbumsByYear(1970, 1979, 0, err, 2);
        check(err.empty() && albums.size() == 3 && albums[2].year == 1979,
              "getAlbumsByYear with no size pages until a short page");
        check(tx.urls.size() == 2 && tx.urls[0].find("getAlbumList2.view") != std::string::npos &&
              tx.urls[0].find("type=byYear&fromYear=1970&toYear=1979&size=2&offset=0") != std::string::npos,
              "getAlbumsByYear asks getAlbumList2 by year with size and offset");
    }
    {
        FakeTransport tx; FakeSettings cfg; cfg.s = basicSettings();
        tx.routes.push_back({"getAlbumList2.view",
            R"({"subsonic-response":{"status":"ok","albumList2":{"album":[{"id":"a9","name":"Z","year":2021}]}}})"});
        SubsonicCore core(tx, cfg);
        std::string err;
        auto albums = core.getAlbumsByYear(9999, 1, 1, err);
        check(err.empty() && albums.size() == 1 && tx.urls.size() == 1 &&
              tx.urls[0].find("fromYear=9999&toYear=1&size=1&offset=0") != std::string::npos,
              "a sized getAlbumsByYear is one request, reverse order kept as asked");
    }
}

struct ScanTransport : navidrome::IHttpTransport {
    std::vector<std::string> statusBodies;
    std::size_t next = 0;
    int sleeps = 0;
    bool failStart = false;

    navidrome::HttpResult getOnce(const std::string& url) override {
        navidrome::HttpResult r;
        if (url.find("startScan.view") != std::string::npos) {
            r.body = failStart
                ? R"({"subsonic-response":{"status":"failed","error":{"code":50,"message":"not admin"}}})"
                : R"({"subsonic-response":{"status":"ok","scanStatus":{"scanning":true,"count":0}}})";
            return r;
        }
        r.body = next < statusBodies.size() ? statusBodies[next++]
               : R"({"subsonic-response":{"status":"ok","scanStatus":{"scanning":false,"count":0}}})";
        return r;
    }
    void sleepMs(int) override { ++sleeps; }
    int  jitterMs() override { return 0; }
};

TEST_CASE(testRunLibraryScan) {
    {
        ScanTransport tx;
        tx.statusBodies = {
            R"({"subsonic-response":{"status":"ok","scanStatus":{"scanning":true,"count":10}}})",
            R"({"subsonic-response":{"status":"ok","scanStatus":{"scanning":false,"count":25}}})",
        };
        FakeSettings cfg; cfg.s = basicSettings();
        navidrome::SubsonicCore core(tx, cfg);
        std::vector<long long> progress;
        std::string err;
        auto done = core.runLibraryScan([&](const navidrome::ScanStatus& st) { progress.push_back(st.count); }, err);
        check(err.empty() && !done.scanning && done.count == 25,
              "runLibraryScan polls until the server stops scanning and returns the final count");
        check(progress.size() == 2 && progress[0] == 10 && progress[1] == 25 && tx.sleeps == 2,
              "every poll waits once and reports its progress");
    }
    {
        ScanTransport tx; tx.failStart = true;
        FakeSettings cfg; cfg.s = basicSettings();
        navidrome::SubsonicCore core(tx, cfg);
        std::string err;
        int calls = 0;
        core.runLibraryScan([&](const navidrome::ScanStatus&) { ++calls; }, err);
        check(!err.empty() && calls == 0 && tx.sleeps == 0, "a refused startScan reports the error without polling");
    }
    check(navidrome::scanProgressText(7) == "Scanning… 7 processed", "scan progress text");
    check(navidrome::scanCompleteText(9) == "Scan complete — 9 items", "scan complete text");
    check(navidrome::scanFailedText("") == "Scan failed: unknown error", "scan failure without a message");
}
}
