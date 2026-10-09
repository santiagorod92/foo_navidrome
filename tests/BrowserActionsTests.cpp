#include "TestHarness.h"
#include "../src/core/NavidromeBrowserModel.h"
#include "FakeBrowserClient.h"

#include <string>
#include <vector>

namespace {

TEST_CASE(testStarRatingSimilarRandom) {
    using navidrome::BrowserNode;
    using navidrome::BrowserNodePtr;

    {
        FakeBrowserClient fc;
        navidrome::Song s; s.id = "song1";
        navidrome::Album a; a.id = "album1";
        navidrome::Artist ar; ar.id = "artist1";
        std::vector<BrowserNodePtr> targets = {
            navidrome::makeSongNode(s),
            navidrome::makeAlbumNode(a),
            navidrome::makeArtistNode(ar),
        };
        auto result = navidrome::applyStarredToNodes(fc, targets, true);
        check(result.done == 3 && result.error.empty(),
              "applyStarredToNodes stars every eligible node");
        check(targets[0]->starred && targets[1]->starred && targets[2]->starred,
              "applyStarredToNodes mutates starred in place on success");
        check(fc.calls[0] == "setStarred:song1:0:1" &&
              fc.calls[1] == "setStarred:album1:1:1" &&
              fc.calls[2] == "setStarred:artist1:2:1",
              "applyStarredToNodes maps node type to the right StarKind");
    }

    {
        FakeBrowserClient fc;
        fc.failIds = { "bad" };
        navidrome::Song s1; s1.id = "bad";
        navidrome::Song s2; s2.id = "good";
        std::vector<BrowserNodePtr> targets = {
            navidrome::makeSongNode(s1), navidrome::makeSongNode(s2),
        };
        auto result = navidrome::applyStarredToNodes(fc, targets, true);
        check(result.done == 1 && result.error == "failed to star bad",
              "applyStarredToNodes keeps going after a failure and reports the first error");
        check(!targets[0]->starred && targets[1]->starred,
              "a failed node's starred flag is left unchanged");
    }

    {
        FakeBrowserClient fc;
        navidrome::Song s; s.id = "song1";
        std::vector<BrowserNodePtr> targets = { navidrome::makeSongNode(s) };
        auto result = navidrome::applyRatingToNodes(fc, targets, 4);
        check(result.done == 1 && result.error.empty() && targets[0]->rating == 4,
              "applyRatingToNodes rates a song and mutates rating in place");
        check(fc.calls.back() == "setRating:song1:4", "applyRatingToNodes forwards stars + id");
    }

    {
        BrowserNode song; song.type = BrowserNode::Song; song.id = "x";
        BrowserNode noId; noId.type = BrowserNode::Song;
        BrowserNode genre; genre.type = BrowserNode::Genre; genre.id = "x";
        check(navidrome::isSimilarEligible(song), "Song with an id is Play-Similar eligible");
        check(!navidrome::isSimilarEligible(noId), "a node with no id is never eligible");
        check(!navidrome::isSimilarEligible(genre), "Genre is not Play-Similar eligible");
    }

    {
        FakeBrowserClient fc;
        std::string err;
        auto similar = navidrome::fetchSimilarSongs(fc, "artist1", 50, err);
        check(err.empty() && similar.size() == 1 && similar[0]->id == "s6" &&
              similar[0]->type == BrowserNode::Song,
              "fetchSimilarSongs maps the fetched songs to song nodes");
        check(fc.calls.back() == "getSimilarSongs:artist1:50",
              "fetchSimilarSongs forwards the item id and count");

        auto random = navidrome::fetchRandomMix(fc, 100, err);
        check(err.empty() && random.size() == 1 && random[0]->id == "s7",
              "fetchRandomMix maps the fetched songs to song nodes");
        check(fc.calls.back() == "getRandomSongs:100", "fetchRandomMix forwards the count");
    }
    {
        FakeBrowserClient fc;
        fc.error = "boom";
        std::string err;
        auto similar = navidrome::fetchSimilarSongs(fc, "x", 50, err);
        check(err == "boom" && similar.empty(),
              "fetchSimilarSongs propagates the client's error with no nodes");
    }
}

TEST_CASE(testInstantMixDropsSeed) {
    std::vector<navidrome::BrowserNodePtr> nodes;
    for (const char* id : { "a", "seed", "b", "seed" }) {
        auto n = std::make_shared<navidrome::BrowserNode>();
        n->type = navidrome::BrowserNode::Song; n->id = id;
        nodes.push_back(n);
    }
    nodes.push_back(nullptr);
    auto out = navidrome::withoutSongId(nodes, "seed");
    check(out.size() == 3 && out[0]->id == "a" && out[1]->id == "b" && !out[2],
          "withoutSongId drops every copy of the seed, keeps order and other entries");
    check(navidrome::withoutSongId(nodes, "zzz").size() == nodes.size(),
          "withoutSongId leaves the list alone when the seed isn't in it");
}
}
