#include "TestHarness.h"
#include "../src/core/NavidromeBrowserModel.h"

#include <string>
#include <vector>

namespace {

TEST_CASE(testHiddenCategories) {
    using navidrome::BrowserNode;

    check(navidrome::browserCategories().size() == 13, "category table lists all 13 root categories");
    check(navidrome::parseHiddenCategories("").empty(), "empty csv hides nothing");

    auto h = navidrome::parseHiddenCategories(" radio,podcasts,,bogus,radio , starred");
    check(h.size() == 3 && h[0] == BrowserNode::CatRadio && h[1] == BrowserNode::CatPodcasts &&
          h[2] == BrowserNode::CatStarred,
          "parse trims, skips unknown/empty keys and de-dupes, keeping csv order");
    check(navidrome::joinHiddenCategories(h) == "starred,radio,podcasts",
          "join writes keys in canonical category order");
    check(navidrome::parseHiddenCategories(navidrome::joinHiddenCategories(h)) ==
              navidrome::CategoryKindList({ BrowserNode::CatStarred, BrowserNode::CatRadio,
                                            BrowserNode::CatPodcasts }),
          "join/parse round-trips");
    check(navidrome::joinHiddenCategories({}).empty(), "nothing hidden joins to an empty string");

    bool keysUnique = true;
    const auto& cats = navidrome::browserCategories();
    for (size_t i = 0; i < cats.size(); ++i)
        for (size_t j = i + 1; j < cats.size(); ++j)
            if (std::string(cats[i].key) == cats[j].key || cats[i].kind == cats[j].kind)
                keysUnique = false;
    check(keysUnique, "category keys and kinds are unique");

    auto nodes = navidrome::buildCategoryNodes({ BrowserNode::CatAllSongs, BrowserNode::CatNowPlaying });
    check(nodes.size() == 11 && nodes.front()->category == BrowserNode::CatStarred &&
          nodes.back()->category == BrowserNode::CatPodcasts,
          "buildCategoryNodes drops hidden kinds and keeps the rest in order");
}

TEST_CASE(testDecadeHelpers) {
    using navidrome::BrowserNode;
    using V = std::vector<int>;

    check(navidrome::decadeStarts(1968, 2024) == V({ 2020, 2010, 2000, 1990, 1980, 1970, 1960 }),
          "decades run newest first and cover both ends");
    check(navidrome::decadeStarts(2024, 1968) == navidrome::decadeStarts(1968, 2024),
          "the two ends can come in either order");
    check(navidrome::decadeStarts(1970, 1979) == V({ 1970 }), "one decade when both ends share it");
    check(navidrome::decadeStarts(0, 2024).empty() && navidrome::decadeStarts(1990, 0).empty(),
          "an unknown year (0) gives no decades");

    auto d = navidrome::makeDecadeNode(1980);
    check(d->type == BrowserNode::Category && d->category == BrowserNode::CatDecade &&
          d->id == "1980" && d->displayName == "1980s" && !navidrome::isLeaf(*d),
          "a decade node is an expandable category keyed by its first year");

    navidrome::Album a; a.id = "al1"; a.name = "Highway to Hell"; a.artist = "AC/DC"; a.year = 1979;
    auto n = navidrome::makeDecadeAlbumNode(a);
    check(n->type == BrowserNode::Album && n->id == "al1" &&
          n->displayName == "AC/DC \u2014 Highway to Hell" && n->subtitle.empty() && n->infoText == "1979",
          "a decade album row names the artist and shows the year");
    check(navidrome::singleColumnLabel(*n) == "AC/DC \u2014 Highway to Hell  1979",
          "the single-column (Windows) label carries artist, album and year");
    navidrome::Album noYear; noYear.id = "al2"; noYear.name = "Untitled";
    auto u = navidrome::makeDecadeAlbumNode(noYear);
    check(u->displayName == "Untitled" && u->infoText.empty(), "no artist and no year leave the name alone");

    std::vector<navidrome::Album> al(4);
    al[0].name = "Powerage"; al[0].artist = "AC/DC"; al[0].year = 1978;
    al[1].name = "Wish You Were Here"; al[1].artist = "Pink Floyd"; al[1].year = 1975;
    al[2].name = "Let There Be Rock"; al[2].artist = "ac/dc"; al[2].year = 1977;
    al[3].name = "Animals"; al[3].artist = "Pink Floyd"; al[3].year = 1977;
    navidrome::sortDecadeAlbums(al);
    check(al[0].name == "Let There Be Rock" && al[1].name == "Powerage" &&
          al[2].name == "Wish You Were Here" && al[3].name == "Animals",
          "decade albums sort by artist (case-insensitive), then year");
}

TEST_CASE(testBrowserModel) {
    using navidrome::BrowserNode;

    auto cats = navidrome::buildCategoryNodes();
    check(cats.size() == 13, "buildCategoryNodes returns All Songs + the 12 smart lists");
    const BrowserNode::CategoryKind expectedOrder[] = {
        BrowserNode::CatAllSongs, BrowserNode::CatStarred, BrowserNode::CatRecentlyAdded,
        BrowserNode::CatMostPlayed, BrowserNode::CatRecentlyPlayed,
        BrowserNode::CatRandom, BrowserNode::CatDecades, BrowserNode::CatGenres,
        BrowserNode::CatPlaylists, BrowserNode::CatBookmarks,
        BrowserNode::CatRadio, BrowserNode::CatPodcasts,
        BrowserNode::CatNowPlaying,
    };
    bool orderOk = cats.size() == 13;
    for (size_t i = 0; i < cats.size() && orderOk; ++i)
        orderOk = cats[i]->type == BrowserNode::Category &&
                  cats[i]->category == expectedOrder[i] &&
                  !cats[i]->displayName.empty();
    check(orderOk, "category nodes are in canonical order with non-empty titles");
    check(cats[0]->displayName == "All Songs", "All Songs heads the category list");
    check(cats[1]->displayName == "\xE2\x98\x85 Starred", "Starred keeps its icon prefix");
    check(cats[9]->category == BrowserNode::CatBookmarks &&
          cats[9]->displayName == "Bookmarks",
          "Bookmarks sits between Playlists and Radio");

    check(navidrome::albumListTypeForCategory(BrowserNode::CatRecentlyAdded) ==
          navidrome::AlbumListType::Newest, "RecentlyAdded -> Newest");
    check(navidrome::albumListTypeForCategory(BrowserNode::CatMostPlayed) ==
          navidrome::AlbumListType::Frequent, "MostPlayed -> Frequent");
    check(navidrome::albumListTypeForCategory(BrowserNode::CatRecentlyPlayed) ==
          navidrome::AlbumListType::Recent, "RecentlyPlayed -> Recent");
    check(navidrome::albumListTypeForCategory(BrowserNode::CatRandom) ==
          navidrome::AlbumListType::Random, "Random -> Random");

    navidrome::Song s;
    s.id = "s1"; s.title = "Song"; s.artist = "A"; s.album = "Alb";
    s.albumId = "alb1"; s.suffix = "flac"; s.track = 4; s.year = 2001;
    s.duration = 183.0; s.starred = true; s.rating = 3;
    auto sn = navidrome::makeSongNode(s, 42000.0);
    check(sn->type == BrowserNode::Song && sn->id == "s1" && sn->album == "Alb" &&
          sn->albumId == "alb1" && sn->suffix == "flac" && sn->track == 4 &&
          sn->rating == 3 && sn->starred && sn->bookmarkPositionMs == 42000.0 &&
          sn->childrenLoaded,
          "makeSongNode copies every field and marks the node a loaded leaf");

    navidrome::Album a; a.id = "al1"; a.name = "Album"; a.artist = "Artist";
    a.coverArtId = "c1"; a.starred = true;
    auto an = navidrome::makeAlbumNode(a);
    check(an->type == BrowserNode::Album && an->id == "al1" &&
          an->subtitle == "Artist" && an->coverArtId == "c1" && an->starred &&
          !an->childrenLoaded,
          "makeAlbumNode maps id/name/artist/cover/starred and stays expandable");

    navidrome::Artist ar; ar.id = "ar1"; ar.name = "The Artist"; ar.starred = false;
    auto arn = navidrome::makeArtistNode(ar);
    check(arn->type == BrowserNode::Artist && arn->id == "ar1" &&
          arn->displayName == "The Artist", "makeArtistNode maps id/name");

    navidrome::Playlist p; p.id = "p1"; p.name = "Mix"; p.songCount = 1;
    check(navidrome::makePlaylistNode(p)->subtitle == "1 track",
          "playlist subtitle is singular for one track");
    p.songCount = 12;
    check(navidrome::makePlaylistNode(p)->subtitle == "12 tracks",
          "playlist subtitle is plural otherwise");

    navidrome::Genre g; g.name = "Jazz"; g.songCount = 7;
    auto gn = navidrome::makeGenreNode(g);
    check(gn->id == "Jazz" && gn->displayName == "Jazz" && gn->subtitle == "7 tracks",
          "genre node keys id off the name (no genre id in Subsonic)");

    navidrome::RadioStation rs; rs.id = "r1"; rs.name = "SomaFM";
    rs.homePageUrl = "https://somafm.com";
    auto rn = navidrome::makeRadioNode(rs);
    check(rn->type == BrowserNode::Radio && rn->subtitle == "https://somafm.com" &&
          rn->childrenLoaded, "radio node is a loaded leaf carrying the home URL");

    auto ln = navidrome::makeLibraryNode("2", "Podcasts");
    check(ln->type == BrowserNode::Library && ln->id == "2" &&
          ln->displayName == "Podcasts", "library node carries folder id + name");

    check(navidrome::isLeaf(*sn) && navidrome::isLeaf(*rn) &&
          !navidrome::isLeaf(*an) && !navidrome::isLeaf(*arn),
          "isLeaf: songs/radio are leaves, artists/albums expand");
    check(navidrome::isAllSongsNode(*cats[0]) && navidrome::isLeaf(*cats[0]) &&
          !navidrome::isLeaf(*cats[1]),
          "All Songs is an enqueue-only leaf; the other categories still expand");

    navidrome::NodeDisplay d = navidrome::nodeDisplay(*sn);
    check(d.name == "\xE2\x98\x85 4. Song",
          "song row: track-number prefix then favorite marker");
    check(d.ratingStars == "\xE2\x98\x85\xE2\x98\x85\xE2\x98\x85",
          "rating renders as N stars, separate from the name");
    check(d.durationText == "3:03", "duration formats as M:SS");
    check(d.bookmarkText.rfind("\xE2\x8F\xB1", 0) == 0 &&
          d.bookmarkText.find("0:42") != std::string::npos,
          "bookmark position renders as a clock glyph + M:SS");

    check(navidrome::singleColumnLabel(*sn) ==
          "\xE2\x98\x85 4. Song  \xE2\x98\x85\xE2\x98\x85\xE2\x98\x85  " + d.bookmarkText,
          "singleColumnLabel joins name + rating + bookmark with two spaces");
    check(navidrome::nodeDisplay(*cats[1]).name == "\xE2\x98\x85 Starred",
          "a starred-looking category title is not double-prefixed");

    navidrome::Song plain; plain.id = "s2"; plain.title = "Plain";
    auto pn = navidrome::makeSongNode(plain);
    navidrome::NodeDisplay pd = navidrome::nodeDisplay(*pn);
    check(pd.name == "Plain" && pd.ratingStars.empty() && pd.bookmarkText.empty() &&
          pd.durationText.empty(),
          "an unrated, unbookmarked, zero-length song shows just its title");
    check(navidrome::singleColumnLabel(*pn) == "Plain",
          "singleColumnLabel adds nothing when there are no markers");

    navidrome::PodcastEpisode pendingEp;
    pendingEp.id = "ep1"; pendingEp.streamId = "s10"; pendingEp.title = "Pending";
    pendingEp.status = "downloading";
    auto pendingNode = navidrome::makePodcastEpisodeNode(pendingEp);
    check(pendingNode->id.empty(),
          "a not-yet-downloaded episode gets no id -> automatically unplayable");
    check(navidrome::nodeDisplay(*pendingNode).infoText == "downloading",
          "an undownloaded episode's status shows as infoText");

    navidrome::PodcastEpisode doneEp;
    doneEp.id = "ep2"; doneEp.streamId = "s11"; doneEp.title = "Done"; doneEp.status = "completed";
    auto doneNode = navidrome::makePodcastEpisodeNode(doneEp);
    check(doneNode->id == "s11" && navidrome::nodeDisplay(*doneNode).infoText.empty(),
          "a completed episode's id is its streamId, with no status annotation");

    navidrome::BrowserNode nowPlaying;
    nowPlaying.type = BrowserNode::Song;
    nowPlaying.infoText = "alice \xC2\xB7 3m ago";
    check(navidrome::singleColumnLabel(nowPlaying) == "  alice \xC2\xB7 3m ago",
          "singleColumnLabel appends infoText (Now Playing's user/time annotation)");
}
}
