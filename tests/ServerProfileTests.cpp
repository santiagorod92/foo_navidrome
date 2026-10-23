#include "TestHarness.h"
#include "../src/core/ServerProfiles.h"

#include <string>

namespace {

navidrome::ServerProfile fields(const std::string& url, const std::string& user) {
    navidrome::ServerProfile p;
    p.url = url;
    p.username = user;
    p.password = "pw-" + user;
    return p;
}

}

TEST_CASE(testServerProfileFormat) {
    navidrome::ServerProfile a;
    a.name = "Home & away"; a.url = "https://music.example.com/"; a.username = "me";
    a.password = "p&ss=wörd\n"; a.customHeaders = "CF-Access-Client-Id: x\nCF-Access-Client-Secret: y";
    a.libraryFilter = true; a.libraryIds = "1,3";
    navidrome::ServerProfile b;
    b.name = "Friend"; b.url = "http://10.0.0.2:4533";
    auto back = navidrome::parseServerProfiles(navidrome::formatServerProfiles({ a, b }));
    check(back.size() == 2 && back[0].name == a.name && back[0].url == a.url && back[0].username == "me" &&
          back[0].password == a.password && back[0].customHeaders == a.customHeaders &&
          back[0].libraryFilter && back[0].libraryIds == "1,3" &&
          back[1].name == "Friend" && !back[1].libraryFilter && back[1].password.empty(),
          "profiles round-trip, including separators and newlines inside values");
    check(navidrome::parseServerProfiles("url=x\n\nname=Ok&junk\n").size() == 1,
          "lines without a name are skipped");
}

TEST_CASE(testServerProfileNames) {
    check(navidrome::serverProfileNameForUrl("https://user@music.example.com:4533/nd/") == "music.example.com" &&
          navidrome::serverProfileNameForUrl("localhost:4533") == "localhost" &&
          navidrome::serverProfileNameForUrl("") == "Navidrome",
          "a migrated profile is named after the server host");
    std::vector<navidrome::ServerProfile> list(2);
    list[0].name = "Home"; list[1].name = "Home 2";
    check(navidrome::uniqueServerProfileName(list, "Home") == "Home 3" &&
          navidrome::uniqueServerProfileName(list, "Work") == "Work",
          "a taken name gets a number");
}

TEST_CASE(testServerProfileSwitching) {
    navidrome::ServerProfileState state;
    auto synced = navidrome::syncActiveServerProfile(state, fields("https://home.example/", "me"));
    check(synced.profiles.size() == 1 && synced.active == "home.example" &&
          synced.profiles[0].username == "me",
          "the configured server becomes the first profile");

    navidrome::ServerProfile added = navidrome::addServerProfile(state, fields("https://home.example/", "me"), "Work");
    check(state.profiles.size() == 2 && state.active == "Work" && added.name == "Work" &&
          added.url == navidrome::kDefaultServerUrl && added.username.empty(),
          "adding keeps the current server and switches to a blank one");

    navidrome::ServerProfile load;
    check(navidrome::switchServerProfile(state, fields("https://work.example/", "boss"), "home.example", load) &&
          state.active == "home.example" && load.url == "https://home.example/" && load.password == "pw-me",
          "switching returns the stored login of the chosen server");
    check(state.profiles[1].url == "https://work.example/" && state.profiles[1].username == "boss",
          "switching away saves what was typed for the previous server");
    check(!navidrome::switchServerProfile(state, fields("https://home.example/", "me"), "home.example", load) &&
          !navidrome::switchServerProfile(state, fields("https://home.example/", "me"), "nope", load),
          "switching to the active or an unknown profile does nothing");

    check(navidrome::removeActiveServerProfile(state, fields("https://home.example/", "me"), load) &&
          state.profiles.size() == 1 && state.active == "Work" && load.username == "boss",
          "removing the active server falls back to the remaining one");
    check(!navidrome::removeActiveServerProfile(state, fields("https://work.example/", "boss"), load) &&
          state.profiles.size() == 1,
          "the last server can't be removed");

    navidrome::ServerProfileState stale;
    stale.profiles.push_back(fields("https://a/", "a"));
    stale.profiles[0].name = "A";
    stale.active = "Gone";
    auto healed = navidrome::syncActiveServerProfile(stale, fields("https://b/", "b"));
    check(healed.profiles.size() == 2 && healed.active == "Gone" && healed.profiles[1].username == "b",
          "an active name with no stored entry is recreated from the current login");
}
