// Unit tests: NavidromeBrowserModel.cpp — buildRootNodes, the fetchChildren dispatch,
// collectSongsDeep / collectSelectionSongs and the playlist rating push-back.
#include "TestHarness.h"
#include "../src/core/NavidromeBrowserModel.h"
#include "FakeBrowserClient.h"

#include <string>
#include <vector>

namespace {

TEST_CASE(testBrowserFetchDispatch) {
    using navidrome::BrowserNode;

    // --- buildRootNodes: flat vs. library-grouped ---
    {
        FakeBrowserClient fc;
        std::string err;
        auto roots = navidrome::buildRootNodes(fc, err);
        check(err.empty(), "flat root load reports no error");
        check(roots.size() == 13 && roots.back()->type == BrowserNode::Artist,
              "flat roots = 12 categories + the artist list");
        check(roots.front()->type == BrowserNode::Category,
              "categories come first in the root list");
    }
    {
        FakeBrowserClient fc; fc.groupIds = {"1", "2"};
        std::string err;
        auto roots = navidrome::buildRootNodes(fc, err);
        check(roots.size() == 14, "grouped roots = 12 categories + 2 library nodes");
        check(roots[12]->type == BrowserNode::Library && roots[12]->id == "1" &&
              roots[12]->displayName == "Music" && roots[13]->displayName == "Podcasts",
              "library nodes carry the folder id and resolved name");
        bool calledGetArtists = false;
        for (auto& c : fc.calls) if (c == "getArtists") calledGetArtists = true;
        check(!calledGetArtists, "grouped load never calls the flat getArtists");
    }
    {
        FakeBrowserClient fc; fc.error = "boom";
        std::string err;
        auto roots = navidrome::buildRootNodes(fc, err);
        check(err == "boom", "a flat-list failure propagates the error");
        check(roots.empty(), "no category nodes are returned on a failed root load");
    }

    // --- fetchChildren: one representative case per node type ---
    struct Case {
        BrowserNode node;
        const char* wantCall;
        BrowserNode::Type wantChildType;
    };
    auto artist = [](const char* lib) {
        BrowserNode n; n.type = BrowserNode::Artist; n.id = "ar1"; n.libraryId = lib; return n;
    };
    auto cat = [](BrowserNode::CategoryKind k) {
        BrowserNode n; n.type = BrowserNode::Category; n.category = k; return n;
    };

    {
        FakeBrowserClient fc; std::string err;
        auto out = navidrome::fetchChildren(fc, artist(""), err);
        check(fc.calls.size() == 1 && fc.calls[0] == "getAlbumsForArtist:",
              "an unpinned artist fetches albums with an empty scope "
              "(Top Songs/Similar Artists are lazy placeholders, no network call yet)");
        check(out.size() == 3 &&
              out[0]->type == BrowserNode::Category &&
              out[0]->category == BrowserNode::CatArtistTopSongs &&
              out[1]->type == BrowserNode::Category &&
              out[1]->category == BrowserNode::CatArtistSimilarArtists &&
              out[2]->type == BrowserNode::Album,
              "-> Top Songs + Similar Artists placeholders, then album nodes");
    }
    {
        FakeBrowserClient fc; std::string err;
        navidrome::fetchChildren(fc, artist("2"), err);
        check(fc.calls[0] == "getAlbumsForArtist:2",
              "an artist under a Library node pins the album scope to that library");
    }
    {
        FakeBrowserClient fc; std::string err;
        BrowserNode lib; lib.type = BrowserNode::Library; lib.id = "2";
        auto out = navidrome::fetchChildren(fc, lib, err);
        check(fc.calls[0] == "getArtistsForLibrary" &&
              out.size() == 1 && out[0]->type == BrowserNode::Artist &&
              out[0]->libraryId == "2",
              "a Library node fetches its artists and pins them to itself");
    }
    {
        FakeBrowserClient fc; std::string err;
        BrowserNode g; g.type = BrowserNode::Genre; g.id = "Rock";
        navidrome::fetchChildren(fc, g, err);
        check(fc.calls[0] == "getSongsForGenre:500",
              "genre expansion asks for up to 500 songs in one request");
    }
    {
        FakeBrowserClient fc; std::string err;
        auto out = navidrome::fetchChildren(fc, cat(BrowserNode::CatBookmarks), err);
        check(fc.calls[0] == "getBookmarks" && out.size() == 1 &&
              out[0]->type == BrowserNode::Song && out[0]->bookmarkPositionMs == 5000,
              "the Bookmarks category yields song nodes carrying the resume position");
    }
    {
        FakeBrowserClient fc; std::string err;
        auto out = navidrome::fetchChildren(fc, cat(BrowserNode::CatPodcasts), err);
        check(fc.calls[0] == "getPodcastChannels" && out.size() == 1 &&
              out[0]->type == BrowserNode::PodcastChannel && out[0]->id == "ch1",
              "the Podcasts category yields channel nodes (a cheap list call)");
    }
    {
        FakeBrowserClient fc; std::string err;
        BrowserNode ch; ch.type = BrowserNode::PodcastChannel; ch.id = "ch1";
        auto out = navidrome::fetchChildren(fc, ch, err);
        check(fc.calls[0] == "getPodcastEpisodes:ch1" && out.size() == 2,
              "a channel node's own expand scopes getPodcastEpisodes to that channel");
        check(out[0]->id == "s8" && out[0]->infoText.empty(),
              "a completed episode is playable (id = streamId) with no status annotation");
        check(out[1]->id.empty() && out[1]->infoText == "downloading",
              "a still-downloading episode has no id (unplayable) and shows its status");
    }
    {
        FakeBrowserClient fc; std::string err;
        auto out = navidrome::fetchChildren(fc, cat(BrowserNode::CatNowPlaying), err);
        check(fc.calls[0] == "getNowPlaying" && out.size() == 1 &&
              out[0]->type == BrowserNode::Song && out[0]->id == "s9" &&
              out[0]->infoText == "alice \xC2\xB7 3m ago",
              "the Now Playing category yields song nodes annotated with user + minutesAgo");
    }
    {
        // Placeholder as built by fetchChildren's Artist case: id = artist id,
        // subtitle = artist name (see makeArtistSubNode).
        FakeBrowserClient fc; std::string err;
        BrowserNode topSongsNode;
        topSongsNode.type = BrowserNode::Category; topSongsNode.category = BrowserNode::CatArtistTopSongs;
        topSongsNode.id = "ar1"; topSongsNode.subtitle = "Artist Name";
        auto out = navidrome::fetchChildren(fc, topSongsNode, err);
        check(fc.calls[0] == "getTopSongs:Artist Name:50" && out.size() == 1 &&
              out[0]->type == BrowserNode::Song && out[0]->id == "s-top",
              "the artist's Top Songs placeholder calls getTopSongs keyed by the artist NAME");
    }
    {
        FakeBrowserClient fc; std::string err;
        BrowserNode similarNode;
        similarNode.type = BrowserNode::Category; similarNode.category = BrowserNode::CatArtistSimilarArtists;
        similarNode.id = "ar1"; similarNode.subtitle = "Artist Name";
        auto out = navidrome::fetchChildren(fc, similarNode, err);
        check(fc.calls[0] == "getArtistInfo:ar1" && out.size() == 1 &&
              out[0]->type == BrowserNode::Artist && out[0]->id == "ar-similar",
              "the artist's Similar Artists placeholder maps getArtistInfo's similarArtists to artist nodes");
    }
    {
        FakeBrowserClient fc; std::string err;
        auto out = navidrome::fetchChildren(fc, cat(BrowserNode::CatAllSongs), err);
        check(fc.calls.size() == 1 && fc.calls[0] == "getAllSongs" && out.size() == 2 &&
              out[0]->type == BrowserNode::Song && out[1]->id == "all2",
              "the All Songs category yields every song via one getAllSongs call");

        FakeBrowserClient fc2;
        std::vector<navidrome::BrowserNodePtr> songs;
        auto allNode = std::make_shared<BrowserNode>(cat(BrowserNode::CatAllSongs));
        navidrome::collectSongsDeep(fc2, allNode, songs);
        check(songs.size() == 2 && songs[0]->id == "all1",
              "Add/Play on the (never-expanded) All Songs node resolves its songs");
    }
    {
        FakeBrowserClient fc; std::string err;
        navidrome::fetchChildren(fc, cat(BrowserNode::CatMostPlayed), err);
        check(fc.calls[0] == std::string("getAlbumList:frequent:100"),
              "Most Played maps to getAlbumList2 frequent, 100 rows");
    }
    {
        FakeBrowserClient fc; std::string err;
        navidrome::fetchChildren(fc, cat(BrowserNode::CatRecentlyAdded), err);
        check(fc.calls[0] == std::string("getAlbumList:newest:100"),
              "Recently Added maps to getAlbumList2 newest");
    }
    {
        FakeBrowserClient fc; fc.error = "net";
        std::string err;
        auto out = navidrome::fetchChildren(fc, cat(BrowserNode::CatStarred), err);
        check(err == "net" && out.empty(),
              "a failed child fetch clears the result and surfaces the error");
    }

    // --- collectSelectionSongs: multi-select de-dupe across selected nodes ---
    {
        auto song = [](const char* id) {
            navidrome::Song s; s.id = id; s.title = id; return navidrome::makeSongNode(s);
        };
        auto loaded = [](std::vector<navidrome::BrowserNodePtr> kids) {
            auto n = std::make_shared<BrowserNode>();
            n->type = BrowserNode::Playlist; n->childrenLoaded = true;
            n->children = std::move(kids);
            return n;
        };
        FakeBrowserClient fc;
        auto a = loaded({ song("x"), song("x"), song("y") });   // playlist with a repeat
        auto b = loaded({ song("y"), song("z") });              // overlaps a on "y"
        auto out = navidrome::collectSelectionSongs(fc, { a, b });
        std::string ids;
        for (auto& s : out) ids += s->id;
        check(ids == "xxyz",
              "collectSelectionSongs drops songs an earlier selected node already "
              "produced, but keeps repeats within one node");
        auto idList = navidrome::collectSongIdsDeep(fc, { a, b });
        check(idList.size() == 4 && idList[3] == "z",
              "collectSongIdsDeep applies the same cross-selection de-dupe");
    }

    // --- collectSongsDeep: recurses through the tree, reuses loaded children ---
    {
        FakeBrowserClient fc;
        auto artistNode = std::make_shared<BrowserNode>();
        artistNode->type = BrowserNode::Artist; artistNode->id = "ar1";
        std::vector<navidrome::BrowserNodePtr> songs;
        navidrome::collectSongsDeep(fc, artistNode, songs);
        // artist -> (fetch) album -> (fetch) song
        check(songs.size() == 1 && songs[0]->type == BrowserNode::Song,
              "collectSongsDeep walks artist -> album -> song via fetches");
        bool touchedSubCategories = false;
        for (auto& c : fc.calls)
            if (c.rfind("getTopSongs", 0) == 0 || c.rfind("getArtistInfo", 0) == 0)
                touchedSubCategories = true;
        check(!touchedSubCategories,
              "collectSongsDeep skips an artist's Top Songs/Similar Artists placeholders "
              "(no duplicate top tracks, no unrelated artists dragged in)");

        FakeBrowserClient fc2;
        auto preloaded = std::make_shared<BrowserNode>();
        preloaded->type = BrowserNode::Album; preloaded->childrenLoaded = true;
        preloaded->children = { navidrome::makeSongNode([]{
            navidrome::Song s; s.id = "x"; s.title = "cached"; return s; }()) };
        songs.clear();
        navidrome::collectSongsDeep(fc2, preloaded, songs);
        check(songs.size() == 1 && songs[0]->id == "x" && fc2.calls.empty(),
              "an already-loaded node is walked from its cached children, no fetch");

        auto ids = navidrome::collectSongIdsDeep(fc, { artistNode });
        check(ids.size() == 1 && ids[0] == "s1",
              "collectSongIdsDeep returns the non-empty song ids");
    }

    // --- syncBrowserNodesToPlaylists: Song filter + RatingUpdate build ---
    {
        navidrome::Song s1; s1.id = "s1"; s1.rating = 4; s1.starred = true;
        navidrome::Song s2; s2.id = "";   s2.rating = 2;      // no id -> skipped
        std::vector<navidrome::BrowserNodePtr> mixed = {
            navidrome::makeSongNode(s1),
            navidrome::makeSongNode(s2),
            navidrome::makeAlbumNode([]{ navidrome::Album a; a.id = "al"; return a; }()),
            navidrome::makeCategoryNode(BrowserNode::CatStarred, "x"),
            nullptr,
        };
        navidrome::g_lastRatingSync.clear();
        navidrome::syncBrowserNodesToPlaylists(mixed);
        check(navidrome::g_lastRatingSync.size() == 1 &&
              navidrome::g_lastRatingSync[0].songId == "s1" &&
              navidrome::g_lastRatingSync[0].rating == 4 &&
              navidrome::g_lastRatingSync[0].starred,
              "syncBrowserNodesToPlaylists forwards only id-bearing Song nodes");
    }
}

} // namespace
