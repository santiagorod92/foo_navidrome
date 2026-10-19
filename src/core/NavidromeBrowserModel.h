#pragma once

#include "SubsonicTypes.h"

#include <algorithm>
#include <cctype>
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
        CatDecades,
        CatDecade,
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
    TrackTags    tags;

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
    n->tags           = s.tags;
    n->bookmarkPositionMs = bookmarkPositionMs;
    n->childrenLoaded = true;
    return n;
}

inline TrackURI trackURIFromNode(const BrowserNode& n) {
    TrackURI t;
    t.id         = n.id;
    t.title      = n.displayName;
    t.artist     = n.subtitle;
    t.album      = n.album;
    t.coverArtId = n.coverArtId;
    t.suffix     = n.suffix;
    t.albumId    = n.albumId;
    t.track      = n.track;
    t.year       = n.year;
    t.rating     = n.rating;
    t.duration   = n.duration;
    t.starred    = n.starred;
    t.tags       = n.tags;
    return t;
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
                    : (c.status == "error" ? trf("Error: {0}", { c.errorMessage }) : c.status);
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
    n->displayName = tr(title);
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
    n->displayName    = tr("Loading…");
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

inline std::vector<int> decadeStarts(int oldestYear, int newestYear) {
    std::vector<int> out;
    if (oldestYear <= 0 || newestYear <= 0) return out;
    if (oldestYear > newestYear) std::swap(oldestYear, newestYear);
    for (int d = newestYear - newestYear % 10; d >= oldestYear - oldestYear % 10; d -= 10)
        out.push_back(d);
    return out;
}

inline std::string decadeLabel(int startYear) {
    return trf("{0}s", { std::to_string(startYear) });
}

inline BrowserNodePtr makeDecadeNode(int startYear) {
    auto n = std::make_shared<BrowserNode>();
    n->type        = BrowserNode::Category;
    n->category    = BrowserNode::CatDecade;
    n->id          = std::to_string(startYear);
    n->displayName = decadeLabel(startYear);
    return n;
}

inline BrowserNodePtr makeDecadeAlbumNode(const Album& a) {
    auto n = makeAlbumNode(a);
    if (!a.artist.empty()) n->displayName = a.artist + " \u2014 " + a.name;
    n->subtitle.clear();
    if (a.year > 0) n->infoText = std::to_string(a.year);
    return n;
}

inline void sortDecadeAlbums(std::vector<Album>& albums) {
    auto lower = [](std::string s) {
        for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    std::stable_sort(albums.begin(), albums.end(), [&](const Album& a, const Album& b) {
        const std::string la = lower(a.artist), lb = lower(b.artist);
        if (la != lb) return la < lb;
        if (a.year != b.year) return a.year < b.year;
        return lower(a.name) < lower(b.name);
    });
}

inline void sortAllSongs(std::vector<Song>& songs) {
    auto lower = [](std::string s) {
        for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    struct Key { std::string artist, album, title; size_t index; };
    std::vector<Key> keys;
    keys.reserve(songs.size());
    for (size_t i = 0; i < songs.size(); ++i) {
        const Song& s = songs[i];
        keys.push_back({ lower(s.tags.albumArtist.empty() ? s.artist : s.tags.albumArtist),
                         lower(s.album), lower(s.title), i });
    }
    std::stable_sort(keys.begin(), keys.end(), [&](const Key& ka, const Key& kb) {
        const Song& a = songs[ka.index];
        const Song& b = songs[kb.index];
        if (ka.artist != kb.artist) return ka.artist < kb.artist;
        if (a.year != b.year) return a.year < b.year;
        if (ka.album != kb.album) return ka.album < kb.album;
        if (a.albumId != b.albumId) return a.albumId < b.albumId;
        if (a.tags.discNumber != b.tags.discNumber) return a.tags.discNumber < b.tags.discNumber;
        if (a.track != b.track) return a.track < b.track;
        return ka.title < kb.title;
    });
    std::vector<Song> sorted;
    sorted.reserve(songs.size());
    for (const auto& k : keys) sorted.push_back(std::move(songs[k.index]));
    songs = std::move(sorted);
}

struct CategoryEntry {
    BrowserNode::CategoryKind kind;
    const char* title;
    const char* key;
};

inline const std::vector<CategoryEntry>& browserCategories() {
    static const std::vector<CategoryEntry> kCategories = {
        { BrowserNode::CatAllSongs,       N_("All Songs"),       "all_songs"       },
        { BrowserNode::CatStarred,        N_("★ Starred"),       "starred"         },
        { BrowserNode::CatRecentlyAdded,  N_("Recently Added"),  "recently_added"  },
        { BrowserNode::CatMostPlayed,     N_("Most Played"),     "most_played"     },
        { BrowserNode::CatRecentlyPlayed, N_("Recently Played"), "recently_played" },
        { BrowserNode::CatRandom,         N_("Random Albums"),   "random_albums"   },
        { BrowserNode::CatDecades,        N_("Decades"),         "decades"         },
        { BrowserNode::CatGenres,         N_("Genres"),          "genres"          },
        { BrowserNode::CatPlaylists,      N_("Playlists"),       "playlists"       },
        { BrowserNode::CatBookmarks,      N_("Bookmarks"),       "bookmarks"       },
        { BrowserNode::CatRadio,          N_("Radio"),           "radio"           },
        { BrowserNode::CatPodcasts,       N_("Podcasts"),        "podcasts"        },
        { BrowserNode::CatNowPlaying,     N_("Now Playing"),     "now_playing"     },
    };
    return kCategories;
}

using CategoryKindList = std::vector<BrowserNode::CategoryKind>;

inline bool containsCategory(const CategoryKindList& list, BrowserNode::CategoryKind kind) {
    for (auto k : list)
        if (k == kind) return true;
    return false;
}

inline CategoryKindList parseHiddenCategories(const std::string& csv) {
    CategoryKindList out;
    std::size_t pos = 0;
    while (pos <= csv.size()) {
        std::size_t comma = csv.find(',', pos);
        if (comma == std::string::npos) comma = csv.size();
        std::string key = csv.substr(pos, comma - pos);
        while (!key.empty() && key.front() == ' ') key.erase(key.begin());
        while (!key.empty() && key.back() == ' ') key.pop_back();
        for (const auto& c : browserCategories())
            if (key == c.key && !containsCategory(out, c.kind)) out.push_back(c.kind);
        pos = comma + 1;
    }
    return out;
}

inline std::string joinHiddenCategories(const CategoryKindList& hidden) {
    std::string out;
    for (const auto& c : browserCategories()) {
        if (!containsCategory(hidden, c.kind)) continue;
        if (!out.empty()) out += ',';
        out += c.key;
    }
    return out;
}

inline std::vector<BrowserNodePtr> buildCategoryNodes(const CategoryKindList& hidden = {}) {
    std::vector<BrowserNodePtr> out;
    out.reserve(browserCategories().size());
    for (const auto& c : browserCategories())
        if (!containsCategory(hidden, c.kind))
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
    virtual std::vector<Album>        getAlbumsByYear(int fromYear, int toYear, int size,
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
    virtual SearchResults              search(const std::string& query, std::string& outError) = 0;

    virtual std::vector<std::string>  groupingLibraryIds() = 0;
    virtual CategoryKindList          hiddenCategories() { return {}; }
    virtual std::vector<MusicFolder>  musicFolders() = 0;

    virtual bool setStarred(bool starred, const std::string& itemId, StarKind kind,
                            std::string& outError) = 0;
    virtual bool setRating(int stars, const std::string& songId,
                           std::string& outError) = 0;

    virtual bool getSong(const std::string& songId, Song& out, std::string& outError) = 0;

    virtual bool getPlayQueue(PlayQueue& out, std::string& outError) = 0;
    virtual bool savePlayQueue(const std::vector<std::string>& songIds, const std::string& currentId,
                               double positionMs, std::string& outError) = 0;
};

std::vector<BrowserNodePtr> buildRootNodes(IBrowserClient& client, std::string& outError);

struct PlayQueueRestorePlan {
    std::vector<BrowserNodePtr> nodes;
    std::size_t                 startIndex = 0;
    double                      positionSeconds = 0.0;
};

PlayQueueRestorePlan planPlayQueueRestore(const PlayQueue& queue);

struct PlayQueueSavePlan {
    std::vector<std::string> songIds;
    std::string              currentId;
    double                   positionMs = 0.0;
    std::size_t              skippedForeign = 0;
    std::size_t              truncated = 0;
};

constexpr std::size_t kNoCurrentTrack = static_cast<std::size_t>(-1);

PlayQueueSavePlan planPlayQueueSave(const std::vector<std::string>& paths,
                                    std::size_t currentIndex, double positionSeconds,
                                    std::size_t maxIds = kPlayQueueMaxIds);

std::string playQueueSaveSummary(const PlayQueueSavePlan& plan);

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

inline std::string downloadFileName(const BrowserNode& n) {
    std::string name;
    if (n.track > 0) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%02d. ", n.track);
        name += buf;
    }
    if (!n.subtitle.empty()) name += n.subtitle + " - ";
    name += n.displayName.empty() ? "untitled" : n.displayName;
    name = sanitizeFileName(name);
    if (!n.suffix.empty()) name += "." + n.suffix;
    return name;
}

inline std::string sentPlaylistSummary(const std::string& name, std::size_t tracks, std::size_t skipped) {
    return skipped > 0
        ? trf("Sent \"{0}\" ({1} tracks, {2} non-Navidrome skipped)",
              { name, std::to_string(tracks), std::to_string(skipped) })
        : trf("Sent \"{0}\" ({1} tracks)", { name, std::to_string(tracks) });
}

inline std::string downloadSummary(std::size_t done, std::size_t failed) {
    return failed == 0
        ? trf("Downloaded {0} track(s)", { std::to_string(done) })
        : trf("Downloaded {0}, {1} failed", { std::to_string(done), std::to_string(failed) });
}

inline std::string rootLoadSummary(const std::vector<BrowserNodePtr>& roots) {
    std::size_t artists = 0, libraries = 0;
    for (const auto& n : roots) {
        if (!n) continue;
        if (n->type == BrowserNode::Artist)  ++artists;
        if (n->type == BrowserNode::Library) ++libraries;
    }
    return libraries ? trf("{0} libraries", { std::to_string(libraries) })
                     : trf("{0} artists", { std::to_string(artists) });
}

constexpr std::size_t kMinSearchQueryLength = 2;

inline bool isSearchQuery(const std::string& query) { return query.size() >= kMinSearchQueryLength; }

std::vector<BrowserNodePtr> searchSongNodes(IBrowserClient& client, const std::string& query,
                                            std::string& outError);

bool listLibraryAlbums(IBrowserClient& client, const std::function<bool()>& aborted,
                       const std::function<void(const Album&)>& onAlbum, std::string& outError);

std::vector<BrowserNodePtr> collectArtistSongs(IBrowserClient& client, const std::string& artistId,
                                               std::string& outError);
}
