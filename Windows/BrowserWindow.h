#pragma once
#include "stdafx.h"
#include "../SubsonicTypes.h"
#include <SDK/coreDarkMode.h>
#include <SDK/ui_element.h>
#include <cstdint>
#include <map>
#include <memory>
#include <vector>
#include <string>

#define WM_NAVIDROME_LOADED  (WM_USER + 101)
#define WM_NAVIDROME_CHILDREN (WM_USER + 102)
// Background refresh of the server playlist list backing the "Add to Navidrome
// Playlist" submenu. wParam owns a heap std::vector<navidrome::Playlist>.
#define WM_NAVIDROME_PLAYLISTS (WM_USER + 103)
// Background refresh of the radio station list backing the enqueue-time
// streamUrl lookup. wParam owns a heap std::vector<navidrome::RadioStation>.
#define WM_NAVIDROME_RADIO (WM_USER + 104)
// Background search result, kept separate from WM_NAVIDROME_LOADED so a
// completed search never overwrites the browse tree's root node list.
#define WM_NAVIDROME_SEARCH (WM_USER + 105)

// ---------------------------------------------------------------------------
// Tree node
// ---------------------------------------------------------------------------
struct NavidromeNode {
    enum Type { Artist, Album, Song, Category, Playlist, Genre, Radio, Loading, Error };
    // Smart-list roots shown above the artist list; each maps to one Subsonic
    // endpoint (see BrowserWindow::fetchChildren).
    enum CategoryKind {
        CatStarred,          // getStarred2.view       → songs
        CatRecentlyAdded,    // getAlbumList2 newest   → albums
        CatMostPlayed,       // getAlbumList2 frequent → albums
        CatRecentlyPlayed,   // getAlbumList2 recent   → albums
        CatRandom,           // getAlbumList2 random   → albums
        CatGenres,           // getGenres.view         → genres
        CatPlaylists,        // getPlaylists.view      → playlists
        CatBookmarks,        // getBookmarks.view      → songs
        CatRadio,            // getInternetRadioStations.view → stations
    };

    Type        type         = Loading;
    CategoryKind category    = CatStarred;   // category nodes only
    std::string id;
    std::string displayName;
    std::string subtitle;    // artist name for albums/songs
    std::string album;       // album name for songs
    std::string coverArtId;
    std::string suffix;      // codec suffix (mp3/flac/…) for songs
    int         track        = 0;
    int         year         = 0;
    double      duration     = 0.0;
    std::string albumId;               // album id (song nodes; startup refresh)
    bool        starred      = false;   // server-side favorite
    int         rating       = 0;       // 0 = unrated, else 1-5
    double      bookmarkPositionMs = 0.0; // > 0 when this song has a saved resume position
    bool        childrenLoaded = false;
    bool        isLoading    = false;
    HTREEITEM   hItem        = nullptr;
    std::vector<std::shared_ptr<NavidromeNode>> children;
};

// Payload sent from background thread to main thread
struct LoadedPayload {
    std::shared_ptr<NavidromeNode>              parent;   // nullptr = root load
    std::vector<std::shared_ptr<NavidromeNode>> nodes;
    std::string                                 error;
    // Search payloads only: the m_searchGeneration value in effect when the
    // request was dispatched. A stale response (superseded by later typing)
    // is dropped instead of clobbering newer results.
    std::uint64_t                               generation = 0;
};

// ---------------------------------------------------------------------------
// BrowserWindow
// ---------------------------------------------------------------------------
// Reads foobar's globally configured UI colors (Preferences > Display >
// Colours and Fonts) via ui_config_manager — independent of dark mode, and
// works from a plain window since it isn't tied to a ui_element instance.
class BrowserWindow : public CWindowImpl<BrowserWindow>, private ui_config_callback_impl {
public:
    static BrowserWindow& get();
    void show();
    // Create as a WS_CHILD panel filling `parent` (used by the Media Library
    // prefs page for an inline browser, mirroring the macOS embedded mount).
    void createEmbedded(HWND parent);

    DECLARE_WND_CLASS(L"foo_navidrome_BrowserWnd")

    BEGIN_MSG_MAP(BrowserWindow)
        MSG_WM_CREATE(OnCreate)
        MSG_WM_DESTROY(OnDestroy)
        MSG_WM_SIZE(OnSize)
        MESSAGE_HANDLER(WM_NAVIDROME_LOADED,    OnNavidromeLoaded)
        MESSAGE_HANDLER(WM_NAVIDROME_CHILDREN,  OnNavidromeChildren)
        MESSAGE_HANDLER(WM_NAVIDROME_PLAYLISTS, OnNavidromePlaylists)
        MESSAGE_HANDLER(WM_NAVIDROME_RADIO,      OnNavidromeRadio)
        MESSAGE_HANDLER(WM_NAVIDROME_SEARCH,     OnNavidromeSearch)
        MSG_WM_TIMER(OnTimer)
        NOTIFY_CODE_HANDLER_EX(TVN_ITEMEXPANDING, OnTreeExpanding)
        NOTIFY_CODE_HANDLER_EX(NM_DBLCLK,        OnTreeDblClick)
        NOTIFY_CODE_HANDLER_EX(NM_RETURN,        OnTreeReturn)
        MSG_WM_CONTEXTMENU(OnContextMenu)
        MSG_WM_ERASEBKGND(OnEraseBkgnd)
        MSG_WM_CTLCOLOREDIT(OnCtlColorEdit)
        MSG_WM_CTLCOLORSTATIC(OnCtlColorStatic)
        COMMAND_ID_HANDLER_EX(IDC_ADD,     OnAdd)
        COMMAND_ID_HANDLER_EX(IDC_PLAY,    OnPlay)
        COMMAND_ID_HANDLER_EX(IDC_PLAY_SIMILAR, OnPlaySimilar)
        COMMAND_ID_HANDLER_EX(IDC_RANDOM_MIX, OnRandomMix)
        COMMAND_ID_HANDLER_EX(IDC_REFRESH, OnRefresh)
        COMMAND_ID_HANDLER_EX(IDC_STAR,    OnStar)
        COMMAND_ID_HANDLER_EX(IDC_UNSTAR,  OnUnstar)
        COMMAND_ID_HANDLER_EX(IDC_SEND_PLAYLIST, OnSendActivePlaylist)
        COMMAND_ID_HANDLER_EX(IDC_NEW_PLAYLIST,     OnNewServerPlaylist)
        COMMAND_ID_HANDLER_EX(IDC_REMOVE_FROM_PL,   OnRemoveFromPlaylist)
        COMMAND_ID_HANDLER_EX(IDC_RENAME_PLAYLIST,  OnRenamePlaylist)
        COMMAND_ID_HANDLER_EX(IDC_DELETE_PLAYLIST,  OnDeletePlaylist)
        COMMAND_ID_HANDLER_EX(IDC_DOWNLOAD,         OnDownload)
        COMMAND_ID_HANDLER_EX(IDC_REMOVE_BOOKMARK,  OnRemoveBookmark)
        COMMAND_ID_HANDLER_EX(IDC_NEW_RADIO,    OnNewRadioStation)
        COMMAND_ID_HANDLER_EX(IDC_EDIT_RADIO,   OnEditRadioStation)
        COMMAND_ID_HANDLER_EX(IDC_DELETE_RADIO, OnDeleteRadioStation)
        COMMAND_RANGE_HANDLER_EX(IDC_RATE_0, IDC_RATE_5, OnRate)
        // One id per server playlist in the "Add to Navidrome Playlist" submenu.
        COMMAND_RANGE_HANDLER_EX(IDC_PLAYLIST_FIRST, IDC_PLAYLIST_LAST,
                                 OnAddToServerPlaylist)
        COMMAND_HANDLER_EX(IDC_SEARCH, EN_CHANGE, OnSearchChanged)
    END_MSG_MAP()

private:
    enum {
        IDC_TREE   = 1001,
        IDC_SEARCH = 1002,
        IDC_ADD    = 1003,
        IDC_PLAY   = 1004,
        IDC_REFRESH= 1005,
        IDC_STATUS = 1006,
        IDC_STAR   = 1007,
        IDC_UNSTAR = 1008,
        IDC_PLAY_SIMILAR = 1025,
        IDC_RANDOM_MIX   = 1026,
        IDC_SEND_PLAYLIST = 1009,
        // Contiguous so a single COMMAND_RANGE_HANDLER_EX covers 0-5 stars.
        IDC_RATE_0 = 1010,
        IDC_RATE_5 = 1015,
        IDC_NEW_PLAYLIST    = 1016,
        IDC_REMOVE_FROM_PL  = 1017,
        IDC_RENAME_PLAYLIST = 1018,
        IDC_DELETE_PLAYLIST = 1019,
        IDC_DOWNLOAD        = 1020,
        IDC_REMOVE_BOOKMARK = 1021,
        IDC_NEW_RADIO       = 1022,
        IDC_EDIT_RADIO      = 1023,
        IDC_DELETE_RADIO    = 1024,
        // One id per entry in the server-playlist submenu; the offset from
        // IDC_PLAYLIST_FIRST indexes m_serverPlaylists.
        IDC_PLAYLIST_FIRST = 1100,
        IDC_PLAYLIST_LAST  = 1299,
    };
    static constexpr std::size_t kMaxPlaylistMenuEntries =
        IDC_PLAYLIST_LAST - IDC_PLAYLIST_FIRST + 1;
    // Search box debounce: a keystroke (re)starts this timer rather than
    // firing a request per character; the search only goes out once typing
    // pauses for kSearchDebounceMs.
    static constexpr UINT_PTR kSearchDebounceTimer = 1;
    static constexpr UINT     kSearchDebounceMs    = 300;

    LRESULT OnCreate(LPCREATESTRUCT);
    void    OnDestroy();
    LRESULT OnSize(UINT, CSize);
    LRESULT OnNavidromeLoaded(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnNavidromeChildren(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnNavidromePlaylists(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnNavidromeRadio(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnNavidromeSearch(UINT, WPARAM, LPARAM, BOOL&);
    void    OnTimer(UINT_PTR id);
    LRESULT OnTreeExpanding(LPNMHDR);
    LRESULT OnTreeDblClick(LPNMHDR);
    LRESULT OnTreeReturn(LPNMHDR);
    void    OnContextMenu(CWindow wnd, CPoint point);
    void    OnAdd(UINT, int, HWND);
    void    OnPlay(UINT, int, HWND);
    void    OnPlaySimilar(UINT, int, HWND);
    void    OnRandomMix(UINT, int, HWND);
    void    OnRefresh(UINT, int, HWND);
    void    OnStar(UINT, int, HWND);
    void    OnUnstar(UINT, int, HWND);
    void    OnRate(UINT, int, HWND);
    void    OnSendActivePlaylist(UINT, int, HWND);
    void    OnSearchChanged(UINT, int, HWND);
    void    OnAddToServerPlaylist(UINT, int, HWND);
    void    OnNewServerPlaylist(UINT, int, HWND);
    void    OnRemoveFromPlaylist(UINT, int, HWND);
    void    OnRenamePlaylist(UINT, int, HWND);
    void    OnDeletePlaylist(UINT, int, HWND);
    void    OnDownload(UINT, int, HWND);
    void    OnRemoveBookmark(UINT, int, HWND);
    void    OnNewRadioStation(UINT, int, HWND);
    void    OnEditRadioStation(UINT, int, HWND);
    void    OnDeleteRadioStation(UINT, int, HWND);

    void    loadArtists();
    void    populateRoot(LoadedPayload* payload);
    void    populateChildren(LoadedPayload* payload);
    // Search results are shown in the same tree control but never touch
    // m_rootNodes, so category invalidation and the browse tree survive a
    // search untouched. restoreBrowseTree() re-renders m_rootNodes when the
    // search box is cleared, without a network round-trip.
    void    populateSearchResults(LoadedPayload* payload);
    void    restoreBrowseTree();
    HTREEITEM insertNode(HTREEITEM parent, std::shared_ptr<NavidromeNode> node);
    std::shared_ptr<NavidromeNode> nodeForItem(HTREEITEM hItem);
    // Synchronous child fetch for any expandable node — background thread only.
    // Shared by lazy expansion and the deep song collector so both agree on
    // what a category / playlist / artist / album contains.
    std::vector<std::shared_ptr<NavidromeNode>>
            fetchChildren(const std::shared_ptr<NavidromeNode>& node, std::string& outError);
    void    collectSongsDeep(std::shared_ptr<NavidromeNode> node,
                             std::vector<std::shared_ptr<NavidromeNode>>& out);
    void    applyStarred(bool starred);
    // Tree label for a node: track number, favorite marker and rating stars.
    std::string labelFor(const std::shared_ptr<NavidromeNode>& node) const;
    void    refreshLabel(const std::shared_ptr<NavidromeNode>& node);
    void    enqueueNodes(std::vector<std::shared_ptr<NavidromeNode>> songs, bool play, bool clearFirst = false);
    std::vector<std::shared_ptr<NavidromeNode>> selectedNodes();
    void    queueSelected(bool play, bool closeAfter, bool clearFirst = false);
    void    setStatus(const std::string& msg);

    // Server playlist management. Selections are resolved to song ids the same
    // way the Add/Play actions resolve them to playable nodes.
    void    refreshServerPlaylists();
    // Background-thread half of the selection resolve: the caller captures the
    // selected nodes on the UI thread (the tree control can only be read there)
    // and this walks them down to song ids, fetching children as needed.
    std::vector<std::string> collectSongIdsDeep(
        const std::vector<std::shared_ptr<NavidromeNode>>& nodes);
    // The single selected Playlist node, or nullptr.
    std::shared_ptr<NavidromeNode> singleSelectedPlaylist();
    // Drop cached children so the next expand refetches from the server.
    void    invalidatePlaylistNode(const std::string& playlistId);
    void    invalidatePlaylistsCategory();
    void    invalidateBookmarksCategory();
    void    reloadNodeChildren(const std::shared_ptr<NavidromeNode>& node);
    void    applyRemoveBookmark();

    // Radio station management. "New" needs no selection (unlike playlists);
    // Edit/Delete require exactly one selected station.
    void    refreshRadioStations();
    // Empty string if the id isn't cached (e.g. stale selection).
    std::string radioStationURL(const std::string& stationId);
    std::shared_ptr<NavidromeNode> singleSelectedRadioStation();
    void    invalidateRadioCategory();
    // Polls playback_can_seek() briefly on a background thread, then seeks on
    // the main thread. Used to resume into a bookmarked position right after
    // enqueueNodes() calls playback_control::start() — the stream isn't
    // necessarily seekable the instant playback starts.
    static void seekWhenReady(double positionSeconds);

    // ui_config_callback: fires when the user changes Colours and Fonts
    // (or toggles dark mode) while the browser is open.
    void    ui_colors_changed() override;
    // Re-reads ui_color_text/background/selection/highlight and pushes them
    // into the tree control + repaint brush. Falls back to system colors for
    // any GUID the user hasn't overridden (query_color() returns false).
    void    refreshThemeColors();
    BOOL    OnEraseBkgnd(HDC dc);
    HBRUSH  OnCtlColorEdit(HDC dc, HWND wnd);
    HBRUSH  OnCtlColorStatic(HDC dc, HWND wnd);

    CTreeViewCtrl m_tree;
    CEdit         m_search;
    CButton       m_addBtn, m_playBtn, m_refreshBtn;
    CStatic       m_status;
    fb2k::CCoreDarkModeHooks m_darkMode;

    // Populated by refreshThemeColors(); *_set is false when that GUID isn't
    // user-overridden, meaning the corresponding control should keep its
    // native system color instead of being forced to a theme value.
    struct ThemeColors {
        bool     textSet = false, bgSet = false;
        COLORREF text = 0, bg = 0;
    } m_theme;
    HBRUSH m_themeBgBrush = nullptr;

    // True when hosted inline in the prefs page (vs. the standalone window);
    // only the standalone window hides itself after an Enter "queue + play".
    bool          m_embedded = false;

    // Keeps nodes alive; HTREEITEM lParam points into these shared_ptrs
    std::map<HTREEITEM, std::shared_ptr<NavidromeNode>> m_nodeMap;
    std::vector<std::shared_ptr<NavidromeNode>>          m_rootNodes;

    // Search state. m_rootNodes above always holds the browse tree, even
    // while a search is displayed — search results live entirely in
    // m_searchResultNodes so nothing else that scans m_rootNodes (playlist/
    // radio/bookmark category invalidation, the startup rating refresh) sees
    // a search result list instead of the real tree.
    bool                                         m_isSearching = false;
    std::vector<std::shared_ptr<NavidromeNode>>  m_searchResultNodes;
    std::uint64_t                                m_searchGeneration = 0;

    // Cached server playlists for the context submenu — built in the background
    // so opening the menu never blocks on the network.
    std::vector<navidrome::Playlist> m_serverPlaylists;
    bool                             m_playlistsLoading = false;

    // Cached radio stations, refreshed alongside m_serverPlaylists — lets the
    // enqueue path resolve a station's streamUrl from just its id without a
    // network round-trip.
    std::vector<navidrome::RadioStation> m_radioStations;
    bool                                  m_radioLoading = false;
};
