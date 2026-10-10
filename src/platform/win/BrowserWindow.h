#pragma once
#include "stdafx.h"
#include "../../core/SubsonicTypes.h"
#include "../../core/NavidromeBrowserModel.h"
#include <SDK/coreDarkMode.h>
#include <SDK/ui_element.h>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <vector>
#include <string>

#define WM_NAVIDROME_LOADED  (WM_USER + 101)
#define WM_NAVIDROME_CHILDREN (WM_USER + 102)
#define WM_NAVIDROME_PLAYLISTS (WM_USER + 103)
#define WM_NAVIDROME_RADIO (WM_USER + 104)
#define WM_NAVIDROME_SEARCH (WM_USER + 105)

using NavidromeNode = navidrome::BrowserNode;

inline HTREEITEM NodeItem(const std::shared_ptr<NavidromeNode>& n) {
    return n ? static_cast<HTREEITEM>(n->viewHandle) : nullptr;
}
inline void SetNodeItem(const std::shared_ptr<NavidromeNode>& n, HTREEITEM h) {
    if (n) n->viewHandle = h;
}

struct LoadedPayload {
    std::shared_ptr<NavidromeNode>              parent;
    std::vector<std::shared_ptr<NavidromeNode>> nodes;
    std::string                                 error;
    std::uint64_t                               generation = 0;
};

class BrowserWindow : public CWindowImpl<BrowserWindow>, private ui_config_callback_impl {
public:
    static BrowserWindow& get();
    static void reloadAllOpen();
    void show();
    void createEmbedded(HWND parent);
    void setContextMenuPassthrough(std::function<bool()> pass) { m_passContextMenu = std::move(pass); }

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
        NOTIFY_CODE_HANDLER_EX(TVN_SELCHANGED,    OnTreeSelChanged)
        NOTIFY_CODE_HANDLER_EX(NM_DBLCLK,        OnTreeDblClick)
        NOTIFY_CODE_HANDLER_EX(NM_RETURN,        OnTreeReturn)
        MSG_WM_CONTEXTMENU(OnContextMenu)
        MSG_WM_ERASEBKGND(OnEraseBkgnd)
        MSG_WM_CTLCOLOREDIT(OnCtlColorEdit)
        MSG_WM_CTLCOLORSTATIC(OnCtlColorStatic)
        COMMAND_ID_HANDLER_EX(IDC_ADD,     OnAdd)
        COMMAND_ID_HANDLER_EX(IDC_PLAY,    OnPlay)
        COMMAND_ID_HANDLER_EX(IDC_PLAY_SIMILAR, OnPlaySimilar)
        COMMAND_ID_HANDLER_EX(IDC_ALCHEMY, OnAlchemy)
        COMMAND_ID_HANDLER_EX(IDC_RANDOM_MIX, OnRandomMix)
        COMMAND_ID_HANDLER_EX(IDC_ARTIST_INFO, OnArtistInfo)
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
        COMMAND_ID_HANDLER_EX(IDC_SUBSCRIBE_PODCAST,   OnSubscribePodcast)
        COMMAND_ID_HANDLER_EX(IDC_UNSUBSCRIBE_PODCAST, OnUnsubscribePodcast)
        COMMAND_RANGE_HANDLER_EX(IDC_RATE_0, IDC_RATE_5, OnRate)
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
        IDC_SUBSCRIBE_PODCAST   = 1027,
        IDC_UNSUBSCRIBE_PODCAST = 1028,
        IDC_ARTIST_INFO         = 1029,
        IDC_ALCHEMY             = 1030,
        IDC_PLAYLIST_FIRST = 1100,
        IDC_PLAYLIST_LAST  = 1299,
    };
    static constexpr std::size_t kMaxPlaylistMenuEntries =
        IDC_PLAYLIST_LAST - IDC_PLAYLIST_FIRST + 1;
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
    LRESULT OnTreeSelChanged(LPNMHDR);
    LRESULT OnTreeDblClick(LPNMHDR);
    LRESULT OnTreeReturn(LPNMHDR);
    void    OnContextMenu(CWindow wnd, CPoint point);
    void    OnAdd(UINT, int, HWND);
    void    OnPlay(UINT, int, HWND);
    void    OnPlaySimilar(UINT, int, HWND);
    void    OnRandomMix(UINT, int, HWND);
    void    OnArtistInfo(UINT, int, HWND);
    void    OnAlchemy(UINT, int, HWND);
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
    void    OnSubscribePodcast(UINT, int, HWND);
    void    OnUnsubscribePodcast(UINT, int, HWND);

    void    loadArtists();
    void    populateRoot(LoadedPayload* payload);
    void    populateChildren(LoadedPayload* payload);
    void    populateSearchResults(LoadedPayload* payload);
    void    restoreBrowseTree();
    HTREEITEM insertNode(HTREEITEM parent, std::shared_ptr<NavidromeNode> node);
    std::shared_ptr<NavidromeNode> nodeForItem(HTREEITEM hItem);
    std::vector<std::shared_ptr<NavidromeNode>>
            fetchChildren(const std::shared_ptr<NavidromeNode>& node, std::string& outError);
    void    applyStarred(bool starred);
    std::string labelFor(const std::shared_ptr<NavidromeNode>& node) const;
    void    refreshLabel(const std::shared_ptr<NavidromeNode>& node);
    void    enqueueNodes(std::vector<std::shared_ptr<NavidromeNode>> songs, bool play, bool clearFirst = false);
    std::vector<std::shared_ptr<NavidromeNode>> selectedNodes();
    void    queueSelected(bool play, bool closeAfter, bool clearFirst = false);
    void    queueNodes(std::vector<std::shared_ptr<NavidromeNode>> nodes,
                       bool play, bool closeAfter, bool clearFirst = false);

    static LRESULT CALLBACK TreeSubclassProc(HWND, UINT, WPARAM, LPARAM,
                                             UINT_PTR, DWORD_PTR);
    bool    onTreeLButtonDown(LPARAM lParam);
    std::vector<HTREEITEM> visibleItems();
    void    clearSelectionExcept(HTREEITEM keep);
    void    selectRange(HTREEITEM from, HTREEITEM to);
    bool    isSelected(HTREEITEM h);
    void    setStatus(const std::string& msg);

    void    refreshServerPlaylists();
    std::vector<std::string> collectSongIdsDeep(
        const std::vector<std::shared_ptr<NavidromeNode>>& nodes);
    std::shared_ptr<NavidromeNode> singleSelectedPlaylist();
    void    invalidatePlaylistNode(const std::string& playlistId);
    void    invalidatePlaylistsCategory();
    void    invalidateBookmarksCategory();
    void    reloadNodeChildren(const std::shared_ptr<NavidromeNode>& node);
    void    applyRemoveBookmark();

    void    refreshRadioStations();
    std::string radioStationURL(const std::string& stationId);
    std::shared_ptr<NavidromeNode> singleSelectedRadioStation();
    void    invalidateRadioCategory();

    std::shared_ptr<NavidromeNode> singleSelectedPodcastChannel();
    void    invalidatePodcastsCategory();

    void    ui_colors_changed() override;
    void    refreshThemeColors();
    BOOL    OnEraseBkgnd(HDC dc);
    HBRUSH  OnCtlColorEdit(HDC dc, HWND wnd);
    HBRUSH  OnCtlColorStatic(HDC dc, HWND wnd);

    CTreeViewCtrl m_tree;
    CEdit         m_search;
    CButton       m_addBtn, m_playBtn, m_refreshBtn;
    CStatic       m_status;
    int           m_lineH = 16;
    fb2k::CCoreDarkModeHooks m_darkMode;

    struct ThemeColors {
        bool     textSet = false, bgSet = false;
        COLORREF text = 0, bg = 0;
    } m_theme;
    HBRUSH m_themeBgBrush = nullptr;

    HTREEITEM     m_selAnchor = nullptr;

    bool          m_embedded = false;
    static std::vector<BrowserWindow*> s_open;
    std::function<bool()> m_passContextMenu;

    std::map<HTREEITEM, std::shared_ptr<NavidromeNode>> m_nodeMap;
    std::vector<std::shared_ptr<NavidromeNode>>          m_rootNodes;
    long long                                            m_treeLoadedAtMs = 0;
    std::string                                          m_reloadNotice;

    bool                                         m_isSearching = false;
    std::vector<std::shared_ptr<NavidromeNode>>  m_searchResultNodes;
    std::uint64_t                                m_searchGeneration = 0;

    std::vector<navidrome::Playlist> m_serverPlaylists;
    bool                             m_playlistsLoading = false;

    std::vector<navidrome::RadioStation> m_radioStations;
    bool                                  m_radioLoading = false;
};
