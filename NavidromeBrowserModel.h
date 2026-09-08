#pragma once
// Shared browser tree model — the Navidrome browser's node type, the smart-list
// category list, the Subsonic-model -> node mappers and the row-label
// formatting, all in one place so the Windows (CTreeViewCtrl) and macOS
// (NSOutlineView) views run identical logic.
//
// SDK-free and UI-toolkit-free: pure C++ over the structs in SubsonicTypes.h,
// so it compiles into the component on every platform and into the standalone
// unit-test host (tests/MediaEnrichmentLogicTests.cpp). The child-fetch
// dispatch and the deep song collector that build on top of this live in
// NavidromeBrowserModel.cpp behind the IBrowserClient seam; the SDK-coupled
// enqueue step lives in NavidromeBrowserEnqueue.h / main.cpp.

#include "SubsonicTypes.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace navidrome {

// ---------------------------------------------------------------------------
// Tree node
// ---------------------------------------------------------------------------
struct BrowserNode {
    enum Type {
        Artist, Album, Song, Category, Playlist, Genre, Radio, Library,
        Loading, Error,
    };
    // Smart-list roots shown above the artist list; each maps to one Subsonic
    // endpoint (see fetchChildren in NavidromeBrowserModel.cpp). Canonical
    // order — the two platforms previously declared these in slightly
    // different orders (harmless only because the raw value is never
    // persisted); this is the single source of truth.
    enum CategoryKind {
        CatStarred,          // getStarred2.view              -> songs
        CatRecentlyAdded,    // getAlbumList2 newest           -> albums
        CatMostPlayed,       // getAlbumList2 frequent         -> albums
        CatRecentlyPlayed,   // getAlbumList2 recent           -> albums
        CatRandom,           // getAlbumList2 random           -> albums
        CatGenres,           // getGenres.view                 -> genres
        CatPlaylists,        // getPlaylists.view              -> playlists
        CatBookmarks,        // getBookmarks.view              -> songs
        CatRadio,            // getInternetRadioStations.view  -> stations
    };

    Type         type       = Loading;
    CategoryKind category    = CatStarred;   // category nodes only
    std::string  id;
    std::string  displayName;
    std::string  subtitle;    // artist name for albums/songs; "N tracks" for playlist/genre
    std::string  album;       // album name for songs
    std::string  albumId;     // album id (song nodes; startup refresh)
    std::string  libraryId;   // set on artist nodes shown under a Library node
    std::string  coverArtId;
    std::string  suffix;      // codec suffix (mp3/flac/...) for songs
    int          track      = 0;
    int          year       = 0;
    double       duration   = 0.0;
    bool         starred    = false;   // server-side favorite
    int          rating     = 0;       // 0 = unrated, else 1-5
    double       bookmarkPositionMs = 0.0; // > 0 when this song has a saved resume position

    bool         childrenLoaded = false;
    bool         isLoading       = false;
    std::vector<std::shared_ptr<BrowserNode>> children;

    // Opaque back-pointer to the platform view item (Win32 HTREEITEM, or a
    // retained Cocoa box). Never dereferenced here — the view layer owns it.
    void*        viewHandle = nullptr;
};

using BrowserNodePtr = std::shared_ptr<BrowserNode>;

// Songs / radio stations / placeholders never expand.
inline bool isLeaf(const BrowserNode& n) {
    return n.type == BrowserNode::Song || n.type == BrowserNode::Radio ||
           n.type == BrowserNode::Loading || n.type == BrowserNode::Error;
}

// ---------------------------------------------------------------------------
// Subsonic model -> node
// ---------------------------------------------------------------------------
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
    n->childrenLoaded = true;   // songs are always leaves
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
    n->id          = g.name;   // getSongsByGenre keys off the name, not an id
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
    n->childrenLoaded = true;   // radio stations are always leaves
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

// ---------------------------------------------------------------------------
// Category list
// ---------------------------------------------------------------------------
// Smart-list roots, shown above the artist list. Each expands lazily like any
// other node, so opening the browser still costs exactly one getArtists call.
inline std::vector<BrowserNodePtr> buildCategoryNodes() {
    struct Entry { BrowserNode::CategoryKind kind; const char* title; };
    static const Entry kCategories[] = {
        { BrowserNode::CatStarred,        "★ Starred"  },
        { BrowserNode::CatRecentlyAdded,  "Recently Added"  },
        { BrowserNode::CatMostPlayed,     "Most Played"     },
        { BrowserNode::CatRecentlyPlayed, "Recently Played" },
        { BrowserNode::CatRandom,         "Random Albums"   },
        { BrowserNode::CatGenres,         "Genres"          },
        { BrowserNode::CatPlaylists,      "Playlists"       },
        { BrowserNode::CatBookmarks,      "Bookmarks"       },
        { BrowserNode::CatRadio,          "Radio"           },
    };
    std::vector<BrowserNodePtr> out;
    out.reserve(sizeof(kCategories) / sizeof(kCategories[0]));
    for (const auto& c : kCategories)
        out.push_back(makeCategoryNode(c.kind, c.title));
    return out;
}

// Maps an AlbumListType-backed category to the smart list it fetches. The
// non-album-list categories (Starred/Genres/Playlists/Bookmarks/Radio) are
// handled separately in fetchChildren.
inline AlbumListType albumListTypeForCategory(BrowserNode::CategoryKind kind) {
    switch (kind) {
        case BrowserNode::CatMostPlayed:     return AlbumListType::Frequent;
        case BrowserNode::CatRecentlyPlayed: return AlbumListType::Recent;
        case BrowserNode::CatRandom:         return AlbumListType::Random;
        default:                             return AlbumListType::Newest;
    }
}

// ---------------------------------------------------------------------------
// Row display
// ---------------------------------------------------------------------------
// M:SS, matching the macOS formatDuration and foobar's own short form.
inline std::string formatDurationMMSS(double seconds) {
    int s = static_cast<int>(seconds);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d:%02d", s / 60, s % 60);
    return buf;
}

// Structured display pieces for one row. The single-column Win32 tree
// concatenates name + ratingStars + bookmarkText; the 3-column Cocoa outline
// puts name / (subtitle + ratingStars [+ bookmarkText]) / durationText in its
// three columns. Keeping the pieces separate lets one function feed both
// without changing either platform's rendered result.
struct NodeDisplay {
    std::string name;          // track-number prefix + favorite marker + title
    std::string subtitle;      // raw node subtitle (artist / "N tracks" / home URL)
    std::string ratingStars;   // "" when unrated, else N x U+2605
    std::string bookmarkText;  // "" unless a resume position is set, else "U+23F1 m:ss"
    std::string durationText;  // "" when duration is 0, else M:SS
};

inline NodeDisplay nodeDisplay(const BrowserNode& n) {
    NodeDisplay d;

    d.name = n.displayName;
    if (n.type == BrowserNode::Song && n.track > 0)
        d.name = std::to_string(n.track) + ". " + d.name;
    // Category rows carry their own icon in the title already.
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

    return d;
}

// The Win32 single-column label: everything the tree can show, in one string.
// Reproduces the old BrowserWindow::labelFor byte-for-byte.
inline std::string singleColumnLabel(const BrowserNode& n) {
    NodeDisplay d = nodeDisplay(n);
    std::string label = d.name;
    if (!d.ratingStars.empty())  label += "  " + d.ratingStars;
    if (!d.bookmarkText.empty()) label += "  " + d.bookmarkText;
    return label;
}

// ---------------------------------------------------------------------------
// Client seam
// ---------------------------------------------------------------------------
// The subset of the Subsonic client the browser tree needs, as an abstract
// interface so the fetch dispatch below is written once. Each platform supplies
// a thin adapter: WinBrowserClient over SubsonicClientWin (Windows/BrowserWindow.cpp),
// MacBrowserClient over the ObjC SubsonicClient (Mac/MacSubsonicBrowserClient.mm).
// `outError` is a human-readable string (empty on success) — the same shape both
// clients already return from these calls; the richer navidrome::Error stays
// available through each client's own lastError().
struct IBrowserClient {
    virtual ~IBrowserClient() = default;

    virtual std::vector<Artist>       getArtists(std::string& outError) = 0;
    virtual std::vector<Artist>       getArtistsForLibrary(const std::string& libraryId,
                                                           std::string& outError) = 0;
    // scopeLibraryId empty => not pinned to a single library.
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

    // Multi-library grouping. groupingLibraryIds() returns 2+ ids only when the
    // tree should show a Library level (see the Decisions note in CLAUDE.md);
    // musicFolders() names them.
    virtual std::vector<std::string>  groupingLibraryIds() = 0;
    virtual std::vector<MusicFolder>  musicFolders() = 0;
};

// The tree's root list: category nodes always, then either one Library node per
// grouping id (multi-library server) or a flat artist list. Mirrors the old
// BrowserWindow::loadArtists / -loadArtists worker body. On a flat-list fetch
// error, `outError` is set and no category nodes are returned (the view shows
// the error) — matching the previous behaviour.
std::vector<BrowserNodePtr> buildRootNodes(IBrowserClient& client, std::string& outError);

// Children of one expandable node (artist -> albums, album -> songs, category ->
// its smart list, library -> its artists, ...). On success the fetched song
// nodes' server-side rating/favorite are pushed onto matching playlist entries
// via navidrome::syncRatingsToPlaylists. On error `outError` is set and the
// result is empty. Background thread only.
std::vector<BrowserNodePtr> fetchChildren(IBrowserClient& client,
                                          const BrowserNode& node,
                                          std::string& outError);

// Walk any expandable node down to its songs (and radio stations), reusing
// already-loaded children and fetching the rest through fetchChildren.
void collectSongsDeep(IBrowserClient& client, const BrowserNodePtr& node,
                      std::vector<BrowserNodePtr>& out);
// collectSongsDeep over a list, then the non-empty song ids.
std::vector<std::string> collectSongIdsDeep(IBrowserClient& client,
                                            const std::vector<BrowserNodePtr>& nodes);

} // namespace navidrome
