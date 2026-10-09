#include "TestHarness.h"
#include "../src/core/NavidromeBrowserModel.h"
#include "FakeBrowserClient.h"

#include <string>
#include <vector>

namespace {

TEST_CASE(testLibraryAlbums) {
    {
        FakeBrowserClient fc;
        std::vector<navidrome::Album> got;
        std::string err;
        const bool ok = navidrome::listLibraryAlbums(fc, [] { return false; },
                                                     [&](const navidrome::Album& a) { got.push_back(a); }, err);
        check(ok && err.empty(), "library listing succeeds");
        check(got.size() == 1, "one album per artist from the fake");
        check(!got.empty() && got[0].artistId == "ar1", "missing artistId filled from the artist");
        check(!got.empty() && got[0].coverArtId == "al1", "missing cover id falls back to the album id");
        check(!got.empty() && got[0].artist == "Artist", "album's own artist name kept");
        check(fc.calls.size() == 2 && fc.calls[1] == "getAlbumsForArtist:",
              "albums fetched unscoped (whole library selection)");
    }
    {
        FakeBrowserClient fc; fc.error = "HTTP 401";
        int n = 0; std::string err;
        const bool ok = navidrome::listLibraryAlbums(fc, nullptr, [&](const navidrome::Album&) { ++n; }, err);
        check(!ok && err == "HTTP 401" && n == 0, "artist-list failure reported, no albums");
    }
    {
        FakeBrowserClient fc;
        int n = 0; std::string err;
        const bool ok = navidrome::listLibraryAlbums(fc, [] { return true; },
                                                     [&](const navidrome::Album&) { ++n; }, err);
        check(!ok && n == 0, "aborted listing stops before the first artist's albums");
    }
}

TEST_CASE(testCollectArtistSongs) {
    FakeBrowserClient fc;
    std::string err;
    auto nodes = navidrome::collectArtistSongs(fc, "ar1", err);
    check(nodes.size() == 1 && nodes[0]->type == navidrome::BrowserNode::Song,
          "artist's albums expanded to song nodes");
}
}
