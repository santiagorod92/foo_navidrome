#include "TestHarness.h"
#include "../src/core/LastFm.h"

#include <string>
#include <vector>

namespace {

using navidrome::lastfm::artistNameCandidates;

TEST_CASE(testLastFmArtistNameCandidates) {
    check(artistNameCandidates("Jackson, Michael") ==
              std::vector<std::string>{ "Jackson, Michael", "Michael Jackson" },
          "Last, First tries the stored name, then First Last");
    check(artistNameCandidates("Beatles, The") ==
              std::vector<std::string>{ "Beatles, The", "The Beatles" },
          "a trailing article moves back to the front");
    check(artistNameCandidates("  Rolling Stones ,The ") ==
              std::vector<std::string>{ "Rolling Stones ,The", "The Rolling Stones" },
          "spaces around the comma are trimmed");
    check(artistNameCandidates("Michael Jackson") == std::vector<std::string>{ "Michael Jackson" },
          "a name without a comma is tried as is");
    check(artistNameCandidates("Crosby, Stills, Nash & Young") ==
              std::vector<std::string>{ "Crosby, Stills, Nash & Young" },
          "two or more commas are a band name, not Last, First");
    check(artistNameCandidates("Jackson,") == std::vector<std::string>{ "Jackson," },
          "an empty half isn't flipped");
    check(artistNameCandidates("  ").empty(), "a blank name has no candidates");
}

TEST_CASE(testLastFmArtistInfoURL) {
    check(navidrome::lastfm::artistInfoURL("Michael Jackson", "KEY", "es") ==
              "https://ws.audioscrobbler.com/2.0/?method=artist.getinfo&artist=Michael%20Jackson"
              "&autocorrect=1&lang=es&api_key=KEY&format=json",
          "artist.getinfo asks for autocorrect, the UI language and JSON");
    check(navidrome::lastfm::artistInfoURL("AC/DC", "KEY", "").find("&artist=AC%2FDC&autocorrect=1&api_key=") !=
              std::string::npos,
          "the artist is percent-encoded and an empty language is left out");
}

TEST_CASE(testLastFmParseArtistInfo) {
    std::string bio, url;
    check(navidrome::lastfm::parseArtistInfo(
              R"({"artist":{"name":"Michael Jackson","url":"https://www.last.fm/music/Michael+Jackson",)"
              R"("bio":{"summary":"Michael Joseph Jackson was a <b>singer</b>. <a href=\"https://www.last.fm/music/Michael+Jackson\">Read more on Last.fm</a>"}}})",
              bio, url) &&
              bio == "Michael Joseph Jackson was a singer ." &&
              url == "https://www.last.fm/music/Michael+Jackson",
          "the summary loses its markup and the Read more link");
    check(navidrome::lastfm::parseArtistInfo(
              R"({"artist":{"bio":{"summary":" <a href=\"https://www.last.fm/music/X\">Read more on Last.fm</a>","content":"Full text"}}})",
              bio, url) && bio == "Full text",
          "an empty summary falls back to the content");
    check(!navidrome::lastfm::parseArtistInfo(
              R"({"error":6,"message":"The artist you supplied could not be found"})", bio, url),
          "a Last.fm error is no biography");
    check(!navidrome::lastfm::parseArtistInfo("", bio, url), "an empty body is no biography");
    check(!navidrome::lastfm::parseArtistInfo(R"({"artist":{"bio":{"summary":""}}})", bio, url),
          "an artist without a biography is no biography");
}

TEST_CASE(testLastFmBiographyFallback) {
    using navidrome::lastfm::withLastFmFallback;
    std::vector<std::string> asked;
    auto fetch = [&](const std::string& url) -> std::string {
        asked.push_back(url);
        if (url.find("artist=Michael%20Jackson&") != std::string::npos)
            return R"({"artist":{"url":"https://www.last.fm/music/Michael+Jackson","bio":{"summary":"King of Pop"}}})";
        return R"({"error":6,"message":"not found"})";
    };

    navidrome::ArtistInfo server;
    server.lastFmUrl = "https://www.last.fm/music/Jackson,+Michael";
    auto info = withLastFmFallback(server, "Jackson, Michael", "KEY", "en", fetch);
    check(asked.size() == 2 && info.biography == "King of Pop" &&
              info.lastFmUrl == "https://www.last.fm/music/Michael+Jackson",
          "an empty server biography is looked up as stored, then as First Last");

    asked.clear();
    navidrome::ArtistInfo withBio;
    withBio.biography = "<p>From the server</p>";
    withBio.lastFmUrl = "https://www.last.fm/music/Server";
    auto kept = withLastFmFallback(withBio, "Jackson, Michael", "KEY", "en", fetch);
    check(kept.biography == "<p>From the server</p>" &&
              kept.lastFmUrl == "https://www.last.fm/music/Server" && asked.empty(),
          "a server biography with a link is kept and Last.fm isn't asked");

    asked.clear();
    navidrome::ArtistInfo bioNoUrl;
    bioNoUrl.biography = "<p>From the server</p>";
    auto linked = withLastFmFallback(bioNoUrl, "Jackson, Michael", "KEY", "en", fetch);
    check(asked.size() == 2 && linked.biography == "<p>From the server</p>" &&
              linked.lastFmUrl == "https://www.last.fm/music/Michael+Jackson",
          "a server biography without a link keeps its text and takes Last.fm's page");

    asked.clear();
    auto unlinked = withLastFmFallback(bioNoUrl, "Nobody", "KEY", "en", fetch);
    check(asked.size() == 1 && unlinked.biography == "<p>From the server</p>" &&
              unlinked.lastFmUrl.empty(),
          "no Last.fm page leaves a server biography without a link");

    asked.clear();
    check(withLastFmFallback(server, "Jackson, Michael", "", "en", fetch).biography.empty() &&
              asked.empty(),
          "without an API key nothing is sent");

    asked.clear();
    auto none = withLastFmFallback(server, "Nobody", "KEY", "en", fetch);
    check(asked.size() == 1 && none.biography.empty() && none.lastFmUrl == server.lastFmUrl,
          "no Last.fm biography leaves the server's answer untouched");
}

}
