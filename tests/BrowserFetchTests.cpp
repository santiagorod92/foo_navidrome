#include "TestHarness.h"
#include "../src/core/NavidromeBrowserModel.h"
#include "FakeBrowserClient.h"

#include <string>
#include <vector>

namespace {

TEST_CASE(testDecadesFetch) {
    using navidrome::BrowserNode;
    auto album = [](const char* id, const char* name, const char* artist, int year) {
        navidrome::Album a; a.id = id; a.name = name; a.artist = artist; a.year = year; return a;
    };

    FakeBrowserClient fc;
    fc.byYearAlbums = { album("a1", "Animals", "Pink Floyd", 1977),
                        album("a2", "Powerage", "AC/DC", 1978),
                        album("a3", "Black Ice", "AC/DC", 2008),
                        album("a4", "Rumours", "Fleetwood Mac", 1977) };
    auto root = navidrome::makeCategoryNode(BrowserNode::CatDecades, "Decades");
    std::string err;
    auto decades = navidrome::fetchChildren(fc, *root, err);
    check(err.empty() && decades.size() == 4 && decades[0]->displayName == "2000s" &&
          decades[3]->displayName == "1970s" && decades[3]->category == BrowserNode::CatDecade,
          "Decades lists every decade between the oldest and the newest album, newest first");
    check(fc.calls.size() == 2 && fc.calls[0] == "getAlbumsByYear:1-9999:1" &&
          fc.calls[1] == "getAlbumsByYear:9999-1:1",
          "Decades finds the year range with two one-album byYear requests");

    fc.calls.clear();
    auto seventies = navidrome::fetchChildren(fc, *decades[3], err);
    check(err.empty() && fc.calls.size() == 1 && fc.calls[0] == "getAlbumsByYear:1970-1979:0",
          "a decade asks for every album of its ten years");
    check(seventies.size() == 3 && seventies[0]->id == "a2" && seventies[1]->id == "a4" &&
          seventies[2]->id == "a1" && seventies[0]->type == BrowserNode::Album &&
          seventies[0]->infoText == "1978",
          "a decade's albums come sorted by artist, as album rows with the year");

    std::vector<navidrome::BrowserNodePtr> songs;
    navidrome::collectSongsDeep(fc, decades[3], songs, &err);
    check(!songs.empty(), "Play/Add on a decade queues its albums' songs");

    FakeBrowserClient empty;
    auto none = navidrome::fetchChildren(empty, *root, err);
    check(err.empty() && none.empty(), "a library without album years shows no decades");

    FakeBrowserClient broken; broken.error = "boom";
    std::string e2;
    auto failed = navidrome::fetchChildren(broken, *root, e2);
    check(e2 == "boom" && failed.empty() && broken.calls.size() == 1,
          "a failed range request stops there and reports the error");
}

TEST_CASE(testBrowserFetchDispatch) {
    using navidrome::BrowserNode;

    {
        FakeBrowserClient fc;
        std::string err;
        auto roots = navidrome::buildRootNodes(fc, err);
        check(err.empty(), "flat root load reports no error");
        check(roots.size() == 14 && roots.back()->type == BrowserNode::Artist,
              "flat roots = 13 categories + the artist list");
        check(roots.front()->type == BrowserNode::Category,
              "categories come first in the root list");
    }
    {
        FakeBrowserClient fc; fc.groupIds = {"1", "2"};
        std::string err;
        auto roots = navidrome::buildRootNodes(fc, err);
        check(roots.size() == 15, "grouped roots = 13 categories + 2 library nodes");
        check(roots[13]->type == BrowserNode::Library && roots[13]->id == "1" &&
              roots[13]->displayName == "Music" && roots[14]->displayName == "Podcasts",
              "library nodes carry the folder id and resolved name");
        bool calledGetArtists = false;
        for (auto& c : fc.calls) if (c == "getArtists") calledGetArtists = true;
        check(!calledGetArtists, "grouped load never calls the flat getArtists");
    }
    {
        FakeBrowserClient fc;
        fc.hidden = { BrowserNode::CatRadio, BrowserNode::CatPodcasts, BrowserNode::CatBookmarks };
        std::string err;
        auto roots = navidrome::buildRootNodes(fc, err);
        check(roots.size() == 11 && roots.back()->type == BrowserNode::Artist,
              "hidden categories are left out of the flat roots");
        bool leaked = false;
        for (auto& r : roots)
            if (r->type == BrowserNode::Category && navidrome::containsCategory(fc.hidden, r->category))
                leaked = true;
        check(!leaked, "no hidden category node reaches the flat roots");
    }
    {
        FakeBrowserClient fc; fc.groupIds = {"1", "2"};
        for (const auto& c : navidrome::browserCategories()) fc.hidden.push_back(c.kind);
        std::string err;
        auto roots = navidrome::buildRootNodes(fc, err);
        check(roots.size() == 2 && roots[0]->type == BrowserNode::Library,
              "every category hidden leaves only the library nodes when grouped");
    }
    {
        FakeBrowserClient fc; fc.error = "boom";
        std::string err;
        auto roots = navidrome::buildRootNodes(fc, err);
        check(err == "boom", "a flat-list failure propagates the error");
        check(roots.empty(), "no category nodes are returned on a failed root load");
    }

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
        auto a = loaded({ song("x"), song("x"), song("y") });
        auto b = loaded({ song("y"), song("z") });
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

    {
        FakeBrowserClient fc;
        auto artistNode = std::make_shared<BrowserNode>();
        artistNode->type = BrowserNode::Artist; artistNode->id = "ar1";
        std::vector<navidrome::BrowserNodePtr> songs;
        navidrome::collectSongsDeep(fc, artistNode, songs);
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

    {
        FakeBrowserClient fc;
        fc.error = "Album not found";
        auto albumNode = std::make_shared<BrowserNode>();
        albumNode->type = BrowserNode::Album; albumNode->id = "gone";
        std::string err;
        auto out = navidrome::collectSelectionSongs(fc, { albumNode }, &err);
        check(out.empty() && err == "Album not found",
              "collectSelectionSongs surfaces the fetch error instead of an empty silence");

        FakeBrowserClient ok;
        std::string none;
        auto got = navidrome::collectSelectionSongs(ok, { albumNode }, &none);
        check(got.size() == 1 && none.empty(), "a clean collect leaves the error empty");
    }

    {
        using navidrome::browserTreeIsStale;
        using navidrome::kBrowserTreeMaxAgeMs;
        const long long t0 = 1000000;
        check(browserTreeIsStale(0, t0), "a tree that never loaded is stale");
        check(!browserTreeIsStale(t0, t0 + kBrowserTreeMaxAgeMs - 1),
              "a tree younger than the max age is fresh");
        check(browserTreeIsStale(t0, t0 + kBrowserTreeMaxAgeMs),
              "a tree at the max age is stale");

        using navidrome::shouldReloadAfterEmptyCollect;
        check(!shouldReloadAfterEmptyCollect(true, true, true),
              "songs were found: never rebuild under the user");
        check(shouldReloadAfterEmptyCollect(false, true, false),
              "nothing found because a fetch failed: rebuild");
        check(shouldReloadAfterEmptyCollect(false, false, true),
              "nothing found from a stale tree: rebuild");
        check(!shouldReloadAfterEmptyCollect(false, false, false),
              "a really empty node in a fresh tree is left alone");

        using navidrome::queueProblemMessage;
        check(queueProblemMessage(true, "", false).empty(),
              "everything queued: no message");
        const auto partial = queueProblemMessage(true, "Album not found", false);
        check(partial.find("only part") != std::string::npos &&
              partial.find("Refresh") != std::string::npos &&
              partial.find("Album not found") != std::string::npos,
              "partial queue: says so, suggests Refresh, quotes the server error");
        const auto reloaded = queueProblemMessage(false, "Album not found", true);
        check(reloaded.find("reloaded") != std::string::npos &&
              reloaded.find("Refresh") != std::string::npos &&
              reloaded.find("Album not found") != std::string::npos,
              "failed + reloaded: explains the reload, suggests Refresh, quotes the error");
        const auto empty = queueProblemMessage(false, "", false);
        check(empty.find("no tracks") != std::string::npos &&
              empty.find("Refresh") != std::string::npos &&
              empty.find("Server error") == std::string::npos,
              "nothing found without an error: suggests Refresh, no error line");
    }

    {
        navidrome::Song s1; s1.id = "s1"; s1.rating = 4; s1.starred = true;
        navidrome::Song s2; s2.id = "";   s2.rating = 2;
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

TEST_CASE(testSearchSongNodes) {
    {
        FakeBrowserClient fc;
        navidrome::Song s; s.id = "s1"; s.title = "Thunderstruck"; s.artist = "AC/DC";
        fc.searchSongs = { s };
        std::string err;
        check(navidrome::searchSongNodes(fc, "t", err).empty() && fc.calls.empty(),
              "a one-character query sends no search");
        auto nodes = navidrome::searchSongNodes(fc, "th", err);
        check(err.empty() && nodes.size() == 1 && nodes[0]->id == "s1" &&
              nodes[0]->displayName == "Thunderstruck",
              "search results become song nodes titled by the song title alone");
    }
    {
        FakeBrowserClient fc;
        fc.searchError = "boom";
        std::string err;
        check(navidrome::searchSongNodes(fc, "abc", err).empty() && err == "boom",
              "an empty search with a server error reports the error");
        navidrome::Song s; s.id = "s2";
        fc.searchSongs = { s };
        check(navidrome::searchSongNodes(fc, "abc", err).size() == 1 && err.empty(),
              "a partial answer with an error keeps the rows and drops the error");
    }
}

TEST_CASE(testRootLoadSummary) {
    using navidrome::BrowserNode;
    auto mk = [](BrowserNode::Type t) { auto n = std::make_shared<BrowserNode>(); n->type = t; return n; };
    std::vector<navidrome::BrowserNodePtr> flat = { mk(BrowserNode::Category), mk(BrowserNode::Artist), mk(BrowserNode::Artist) };
    check(navidrome::rootLoadSummary(flat) == "2 artists", "flat roots count artists, not categories");
    std::vector<navidrome::BrowserNodePtr> grouped = { mk(BrowserNode::Category), mk(BrowserNode::Library), mk(BrowserNode::Library), mk(BrowserNode::Library) };
    check(navidrome::rootLoadSummary(grouped) == "3 libraries", "grouped roots count libraries");
    check(navidrome::rootLoadSummary({}) == "0 artists", "an empty server reads 0 artists");
}

TEST_CASE(testBrowserActionTexts) {
    navidrome::BrowserNode n;
    n.track = 3; n.subtitle = "AC/DC"; n.displayName = "T.N.T."; n.suffix = "flac";
    check(navidrome::downloadFileName(n) == navidrome::sanitizeFileName("03. AC/DC - T.N.T.") + ".flac",
          "download name = NN. artist - title, sanitized, plus the stored suffix");
    navidrome::BrowserNode bare;
    check(navidrome::downloadFileName(bare) == "untitled", "a node without title or suffix downloads as untitled");
    check(navidrome::downloadSummary(4, 0) == "Downloaded 4 track(s)", "all downloads ok");
    check(navidrome::downloadSummary(3, 1) == "Downloaded 3, 1 failed", "some downloads failed");
    check(navidrome::sentPlaylistSummary("Mix", 10, 0) == "Sent \"Mix\" (10 tracks)", "upload summary");
    check(navidrome::sentPlaylistSummary("Mix", 10, 2) == "Sent \"Mix\" (10 tracks, 2 non-Navidrome skipped)",
          "upload summary counts skipped foreign tracks");
}
}
