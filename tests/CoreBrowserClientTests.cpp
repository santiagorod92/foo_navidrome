#include "TestHarness.h"
#include "../src/core/CoreBrowserClient.h"

#include <string>
#include <utility>
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
    navidrome::SubsonicSettings load() const override {
        navidrome::SubsonicSettings s;
        s.serverUrl = "http://h";
        s.username  = "u";
        s.password  = "p";
        s.salt      = "s";
        return s;
    }
};

}

TEST_CASE(testCoreBrowserClient) {
    using navidrome::CoreBrowserClient;

    {
        RouteTransport tx;
        tx.routes.push_back({"getArtists.view",
            R"({"subsonic-response":{"status":"ok","artists":{"index":[{"name":"A","artist":[{"id":"ar1","name":"Abba","albumCount":3}]}]}}})"});
        StaticSettings cfg;
        navidrome::SubsonicCore core(tx, cfg);
        CoreBrowserClient client(core);

        std::string err;
        auto artists = client.getArtists(err);
        check(err.empty() && artists.size() == 1 && artists[0].id == "ar1" && artists[0].name == "Abba",
              "CoreBrowserClient forwards getArtists to the core and returns its parsed rows");
        check(client.hiddenCategories().empty(),
              "CoreBrowserClient hides nothing without a hidden-categories callback");
    }

    {
        RouteTransport tx;
        StaticSettings cfg;
        navidrome::SubsonicCore core(tx, cfg);
        CoreBrowserClient client(core, [] {
            return navidrome::parseHiddenCategories("starred,radio");
        });
        check(client.hiddenCategories() == navidrome::parseHiddenCategories("starred,radio"),
              "CoreBrowserClient returns what the hidden-categories callback reads");

        navidrome::Song song;
        std::string err;
        const size_t before = tx.urls.size();
        check(!client.getSong("", song, err) && err == "song not found" && tx.urls.size() == before,
              "getSong with an empty id fails locally with 'song not found' and sends nothing");
    }

    {
        RouteTransport tx;
        tx.routes.push_back({"setRating.view",
            R"({"subsonic-response":{"status":"failed","error":{"code":70,"message":"Song not found"}}})"});
        StaticSettings cfg;
        navidrome::SubsonicCore core(tx, cfg);
        CoreBrowserClient client(core);

        std::string err;
        check(!client.setRating(4, "s1", err) && !err.empty(),
              "setRating reports the server's failure with a non-empty error");
    }
}
