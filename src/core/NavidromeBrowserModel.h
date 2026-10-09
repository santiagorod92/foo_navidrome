#pragma once

#include "SubsonicTypes.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace navidrome {

struct BrowserNode {
    enum Type {
        Artist, Album, Song, Category, Playlist, Genre, Radio, Library,
        PodcastChannel,
        Loading, Error,
    };
    enum CategoryKind {
        CatStarred,
        CatRecentlyAdded,
        CatMostPlayed,
        CatRecentlyPlayed,
        CatRandom,
        CatGenres,
        CatPlaylists,
        CatBookmarks,
        CatRadio,
        CatPodcasts,
        CatNowPlaying,
        CatArtistTopSongs,
        CatArtistSimilarArtists,
        CatAllSongs,
    };

    Type         type       = Loading;
    CategoryKind category    = CatStarred;
    std::string  id;
    std::string  displayName;
    std::string  subtitle;
    std::string  album;
    std::string  albumId;
    std::string  libraryId;
    std::string  coverArtId;
    std::string  suffix;
    int          track      = 0;
    int          year       = 0;
    double       duration   = 0.0;
    bool         starred    = false;
    int          rating     = 0;
    double       bookmarkPositionMs = 0.0;
    std::string  infoText;

    bool         childrenLoaded = false;
    bool         isLoading       = false;
    std::vector<std::shared_ptr<BrowserNode>> children;

    void*        viewHandle = nullptr;
};

using BrowserNodePtr = std::shared_ptr<BrowserNode>;

inline bool isAllSongsNode(const BrowserNode& n) {
    return n.type == BrowserNode::Category && n.category == BrowserNode::CatAllSongs;
}

inline bool isLeaf(const BrowserNode& n) {
    return n.type == BrowserNode::Song || n.type == BrowserNode::Radio ||
           n.type == BrowserNode::Loading || n.type == BrowserNode::Error ||
           isAllSongsNode(n);
}

inline std::string trackCountSubtitle(int songCount) {
    return songCount == 1 ? "1 track" : std::to_string(songCount) + " tracks";
}

inline BrowserNodePtr makeArtistNode(const Artist& a) {
    auto n = std::make_shared<BrowserNode>();
    n->type        = BrowserNode::Artist;
    n->id          = a.id;
    n->displayName = a.name;
    n->coverArtId  = a.coverArtId;
    n->starred     = a.starred;
    return n;
}

inline BrowserNodePtr makeAlbumNode(const Album& a) {
    auto n = std::make_shared<BrowserNode>();
    n->type        = BrowserNode::Album;
    n->id          = a.id;
    n->displayName = a.name;
    n->subtitle    = a.artist;
    n->coverArtId  = a.coverArtId;
    n->starred     = a.starred;
    return n;
}

inline BrowserNodePtr makeSongNode(const Song& s, double bookmarkPositionMs = 0.0) {
    auto n = std::make_shared<BrowserNode>();
    n->type           = BrowserNode::Song;
    n->id             = s.id;
    n->displayName    = s.title;
    n->subtitle       = s.artist;
    n->album          = s.album;
    n->albumId        = s.albumId;
    n->coverArtId     = s.coverArtId;
    n->suffix         = s.suffix;
    n->track          = s.track;
    n->year           = s.year;
    n->duration       = s.duration;
    n->starred        = s.starred;
    n->rating         = s.rating;
    n->bookmarkPositionMs = bookmarkPositionMs;
    n->childrenLoaded = true;
    return n;
}

inline BrowserNodePtr makePlaylistNode(const Playlist& p) {
    auto n = std::make_shared<BrowserNode>();
    n->type        = BrowserNode::Playlist;
    n->id          = p.id;
    n->displayName = p.name;
    n->subtitle    = trackCountSubtitle(p.songCount);
    return n;
}

inline BrowserNodePtr makeGenreNode(const Genre& g) {
    auto n = std::make_shared<BrowserNode>();
    n->type        = BrowserNode::Genre;
    n->id          = g.name;
    n->displayName = g.name;
    n->subtitle    = trackCountSubtitle(g.songCount);
    return n;
}

inline BrowserNodePtr makeRadioNode(const RadioStation& s) {
    auto n = std::make_shared<BrowserNode>();
    n->type           = BrowserNode::Radio;
    n->id             = s.id;
    n->displayName    = s.name;
    n->subtitle       = s.homePageUrl;
    n->childrenLoaded = true;
    return n;
}

inline BrowserNodePtr makePodcastChannelNode(const PodcastChannel& c) {
    auto n = std::make_shared<BrowserNode>();
    n->type        = BrowserNode::PodcastChannel;
    n->id          = c.id;
    n->displayName = c.title;
    n->subtitle    = !c.description.empty() ? c.description
                    : (c.status == "error" ? ("Error: " + c.errorMessage) : c.status);
    return n;
}

inline BrowserNodePtr makePodcastEpisodeNode(const PodcastEpisode& e) {
    auto n = std::make_shared<BrowserNode>();
    n->type           = BrowserNode::Song;
    n->id             = (e.status == "completed") ? e.streamId : "";
    n->displayName    = e.title;
    n->duration       = e.duration;
    n->infoText       = (e.status == "completed") ? "" : e.status;
    n->childrenLoaded = true;
    return n;
}

inline BrowserNodePtr makeLibraryNode(const std::string& id, const std::string& name) {
    auto n = std::make_shared<BrowserNode>();
    n->type        = BrowserNode::Library;
    n->id          = id;
    n->displayName = name;
    return n;
}

inline BrowserNodePtr makeCategoryNode(BrowserNode::CategoryKind kind,
                                       const std::string& title) {
    auto n = std::make_shared<BrowserNode>();
    n->type        = BrowserNode::Category;
    n->category    = kind;
    n->displayName = title;
    return n;
}

inline BrowserNodePtr makeArtistSubNode(BrowserNode::CategoryKind kind, const std::string& title,
                                        const std::string& artistId, const std::string& artistName) {
    auto n = makeCategoryNode(kind, title);
    n->id       = artistId;
    n->subtitle = artistName;
    return n;
}

inline BrowserNodePtr loadingNode() {
    auto n = std::make_shared<BrowserNode>();
    n->type           = BrowserNode::Loading;
    n->displayName    = "Loading…";
    n->childrenLoaded = true;
    return n;
}

inline BrowserNodePtr errorNode(const std::string& msg) {
    auto n = std::make_shared<BrowserNode>();
    n->type           = BrowserNode::Error;
    n->displayName    = msg;
    n->childrenLoaded = true;
    return n;
}

inline std::vector<BrowserNodePtr> buildCategoryNodes() {
    struct Entry { BrowserNode::CategoryKind kind; const char* title; };
    static const Entry kCategories[] = {
        { BrowserNode::CatAllSongs,       "All Songs"       },
        { BrowserNode::CatStarred,        "★ Starred"  },
        { BrowserNode::CatRecentlyAdded,  "Recently Added"  },
        { BrowserNode::CatMostPlayed,     "Most Played"     },
        { BrowserNode::CatRecentlyPlayed, "Recently Played" },
        { BrowserNode::CatRandom,         "Random Albums"   },
        { BrowserNode::CatGenres,         "Genres"          },
        { BrowserNode::CatPlaylists,      "Playlists"       },
        { BrowserNode::CatBookmarks,      "Bookmarks"       },
        { BrowserNode::CatRadio,          "Radio"           },
        { BrowserNode::CatPodcasts,       "Podcasts"        },
        { BrowserNode::CatNowPlaying,     "Now Playing"     },
    };
    std::vector<BrowserNodePtr> out;
    out.reserve(sizeof(kCategories) / sizeof(kCategories[0]));
    for (const auto& c : kCategories)
        out.push_back(makeCategoryNode(c.kind, c.title));
    return out;
}

inline AlbumListType albumListTypeForCategory(BrowserNode::CategoryKind kind) {
    switch (kind) {
        case BrowserNode::CatMostPlayed:     return AlbumListType::Frequent;
        case BrowserNode::CatRecentlyPlayed: return AlbumListType::Recent;
        case BrowserNode::CatRandom:         return AlbumListType::Random;
        default:                             return AlbumListType::Newest;
    }
}

inline std::string formatDurationMMSS(double seconds) {
    int s = static_cast<int>(seconds);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d:%02d", s / 60, s % 60);
    return buf;
}

struct NodeDisplay {
    std::string name;
    std::string subtitle;
    std::string ratingStars;
    std::string bookmarkText;
    std::string durationText;
    std::string infoText;
};

inline NodeDisplay nodeDisplay(const BrowserNode& n) {
    NodeDisplay d;

    d.name = n.displayName;
    if (n.type == BrowserNode::Song && n.track > 0)
        d.name = std::to_string(n.track) + ". " + d.name;
    if (n.starred && n.type != BrowserNode::Category)
        d.name = "★ " + d.name;

    d.subtitle = n.subtitle;

    if (n.rating > 0)
        for (int i = 0; i < n.rating; ++i) d.ratingStars += "★";

    if (n.bookmarkPositionMs > 0) {
        int totalSeconds = static_cast<int>(n.bookmarkPositionMs / 1000.0);
        char buf[24];
        std::snprintf(buf, sizeof(buf), "⏱ %d:%02d",
                      totalSeconds / 60, totalSeconds % 60);
        d.bookmarkText = buf;
    }

    if (n.duration > 0)
        d.durationText = formatDurationMMSS(n.duration);

    d.infoText = n.infoText;

    return d;
}

inline std::string singleColumnLabel(const BrowserNode& n) {
    NodeDisplay d = nodeDisplay(n);
    std::string label = d.name;
    if (!d.ratingStars.empty())  label += "  " + d.ratingStars;
    if (!d.bookmarkText.empty()) label += "  " + d.bookmarkText;
    if (!d.infoText.empty())     label += "  " + d.infoText;
    return label;
}

struct IBrowserClient {
    virtual ~IBrowserClient() = default;

    virtual std::vector<Artist>       getArtists(std::string& outError) = 0;
    virtual std::vector<Artist>       getArtistsForLibrary(const std::string& libraryId,
                                                           std::string& outError) = 0;
    virtual std::vector<Album>        getAlbumsForArtist(const std::string& artistId,
                                                         const std::string& scopeLibraryId,
                                                         std::string& outError) = 0;
    virtual std::vector<Song>         getSongsForAlbum(const std::string& albumId,
                                                       std::string& outError) = 0;
    virtual std::vector<Song>         getPlaylistSongs(const std::string& playlistId,
                                                       std::string& outError) = 0;
    virtual std::vector<Song>         getSongsForGenre(const std::string& genre, int count,
                                                       std::string& outError) = 0;
    virtual std::vector<Song>         getStarredSongs(std::string& outError) = 0;
    virtual std::vector<Genre>        getGenres(std::string& outError) = 0;
    virtual std::vector<Playlist>     getPlaylists(std::string& outError) = 0;
    virtual std::vector<Album>        getAlbumList(AlbumListType type, int size,
                                                  std::string& outError) = 0;
    virtual std::vector<RadioStation> getRadioStations(std::string& outError) = 0;
    virtual std::vector<Bookmark>     getBookmarks(std::string& outError) = 0;
    virtual std::vector<PodcastChannel> getPodcastChannels(std::string& outError) = 0;
    virtual std::vector<PodcastEpisode> getPodcastEpisodes(const std::string& channelId,
                                                            std::string& outError) = 0;
    virtual std::vector<NowPlayingEntry> getNowPlaying(std::string& outError) = 0;
    virtual std::vector<Song>         getSimilarSongs(const std::string& itemId, int count,
                                                      std::string& outError) = 0;
    virtual std::vector<Song>         getRandomSongs(int count, std::string& outError) = 0;
    virtual std::vector<Song>         getAllSongs(std::string& outError) = 0;
    virtual ArtistInfo                 getArtistInfo(const std::string& artistId,
                                                      std::string& outError) = 0;
    virtual std::vector<Song>         getTopSongs(const std::string& artistName, int count,
                                                  std::string& outError) = 0;
    virtual Lyrics                     getLyrics(const std::string& songId, const std::string& artist,
                                                 const std::string& title, std::string& outError) = 0;

    virtual std::vector<std::string>  groupingLibraryIds() = 0;
    virtual std::vector<MusicFolder>  musicFolders() = 0;

    virtual bool setStarred(bool starred, const std::string& itemId, StarKind kind,
                            std::string& outError) = 0;
    virtual bool setRating(int stars, const std::string& songId,
                           std::string& outError) = 0;

    virtual bool getSong(const std::string& songId, Song& out, std::string& outError) = 0;
};

std::vector<BrowserNodePtr> buildRootNodes(IBrowserClient& client, std::string& outError);

Lyrics lyricsForTrackURI(IBrowserClient& client, const std::string& uri, std::string& outError);
LyricsCache& lyricsCache();

std::vector<BrowserNodePtr> fetchChildren(IBrowserClient& client,
                                          const BrowserNode& node,
                                          std::string& outError);

void collectSongsDeep(IBrowserClient& client, const BrowserNodePtr& node,
                      std::vector<BrowserNodePtr>& out, std::string* error = nullptr);
std::vector<BrowserNodePtr> collectSelectionSongs(IBrowserClient& client,
                                                  const std::vector<BrowserNodePtr>& nodes,
                                                  std::string* error = nullptr);

constexpr long long kBrowserTreeMaxAgeMs = 30LL * 60 * 1000;

inline long long browserNowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

inline bool browserTreeIsStale(long long loadedAtMs, long long nowMs) {
    return loadedAtMs <= 0 || nowMs - loadedAtMs >= kBrowserTreeMaxAgeMs;
}

inline bool shouldReloadAfterEmptyCollect(bool gotSongs, bool hadError, bool treeStale) {
    return !gotSongs && (hadError || treeStale);
}

std::string queueProblemMessage(bool gotSongs, const std::string& error, bool reloaded);
std::vector<std::string> collectSongIdsDeep(IBrowserClient& client,
                                            const std::vector<BrowserNodePtr>& nodes);

void syncBrowserNodesToPlaylists(const std::vector<BrowserNodePtr>& nodes);

struct StarRatingResult {
    std::size_t done = 0;
    std::string error;
};

StarRatingResult applyStarredToNodes(IBrowserClient& client,
                                     const std::vector<BrowserNodePtr>& targets,
                                     bool starred);

StarRatingResult applyRatingToNodes(IBrowserClient& client,
                                    const std::vector<BrowserNodePtr>& targets,
                                    int stars);

inline bool isSimilarEligible(const BrowserNode& n) {
    return !n.id.empty() &&
           (n.type == BrowserNode::Artist || n.type == BrowserNode::Album ||
            n.type == BrowserNode::Song);
}

inline std::vector<BrowserNodePtr> withoutSongId(std::vector<BrowserNodePtr> nodes,
                                                 const std::string& songId) {
    nodes.erase(std::remove_if(nodes.begin(), nodes.end(),
                               [&](const BrowserNodePtr& n) { return n && n->id == songId; }),
                nodes.end());
    return nodes;
}

std::vector<BrowserNodePtr> fetchSimilarSongs(IBrowserClient& client,
                                              const std::string& itemId, int count,
                                              std::string& outError);

std::vector<BrowserNodePtr> fetchRandomMix(IBrowserClient& client, int count,
                                           std::string& outError);

bool listLibraryAlbums(IBrowserClient& client, const std::function<bool()>& aborted,
                       const std::function<void(const Album&)>& onAlbum, std::string& outError);

std::vector<BrowserNodePtr> collectArtistSongs(IBrowserClient& client, const std::string& artistId,
                                               std::string& outError);
}
