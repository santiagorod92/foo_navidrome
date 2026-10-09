#include "TestHarness.h"
#include "../src/core/NavidromeBrowserModel.h"

#include <string>
#include <vector>

namespace {

TEST_CASE(testBrowserModel) {
    using navidrome::BrowserNode;

    auto cats = navidrome::buildCategoryNodes();
    check(cats.size() == 12, "buildCategoryNodes returns All Songs + the 11 smart lists");
    const BrowserNode::CategoryKind expectedOrder[] = {
        BrowserNode::CatAllSongs, BrowserNode::CatStarred, BrowserNode::CatRecentlyAdded,
        BrowserNode::CatMostPlayed, BrowserNode::CatRecentlyPlayed,
        BrowserNode::CatRandom, BrowserNode::CatGenres,
        BrowserNode::CatPlaylists, BrowserNode::CatBookmarks,
        BrowserNode::CatRadio, BrowserNode::CatPodcasts,
        BrowserNode::CatNowPlaying,
    };
    bool orderOk = cats.size() == 12;
    for (size_t i = 0; i < cats.size() && orderOk; ++i)
        orderOk = cats[i]->type == BrowserNode::Category &&
                  cats[i]->category == expectedOrder[i] &&
                  !cats[i]->displayName.empty();
    check(orderOk, "category nodes are in canonical order with non-empty titles");
    check(cats[0]->displayName == "All Songs", "All Songs heads the category list");
    check(cats[1]->displayName == "\xE2\x98\x85 Starred", "Starred keeps its icon prefix");
    check(cats[8]->category == BrowserNode::CatBookmarks &&
          cats[8]->displayName == "Bookmarks",
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
