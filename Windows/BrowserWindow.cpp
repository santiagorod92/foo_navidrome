#include "stdafx.h"
#include "BrowserWindow.h"
#include "SubsonicClientWin.h"
#include "NavidromeInputWin.h"
#include <SDK/playlist.h>
#include <SDK/metadb.h>
#include <SDK/playable_location.h>
#include <SDK/playback_control.h>
#include <commctrl.h>
#include <shlobj.h>
#include <algorithm>
#include <cstdio>
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

static std::wstring u8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    if (!w.empty() && w.back() == 0) w.pop_back();
    return w;
}

static std::string wToU8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
    if (!s.empty() && s.back() == 0) s.pop_back();
    return s;
}

// ---------------------------------------------------------------------------
// Modal single-line text prompt
//
// Win32 has no InputBox, and a .rc dialog template would drag a resource script
// into a project that deliberately builds without one — so this is a plain
// popup window driven by its own message pump.
// ---------------------------------------------------------------------------
namespace {

class TextPromptWindow : public CWindowImpl<TextPromptWindow> {
public:
    DECLARE_WND_CLASS(L"foo_navidrome_PromptWnd")

    // Returns true and fills `out` when the user confirms.
    static bool run(HWND owner, const wchar_t* title, const wchar_t* label,
                    const std::wstring& initial, std::wstring& out) {
        TextPromptWindow w;
        w.m_label = label;
        w.m_value = initial;

        w.Create(owner, CWindow::rcDefault, title,
                 WS_POPUP | WS_CAPTION | WS_SYSMENU, WS_EX_DLGMODALFRAME);
        if (!w.IsWindow()) return false;

        // Center on the owner (or the screen when there isn't one).
        RECT rcOwner{};
        if (owner && ::GetWindowRect(owner, &rcOwner)) {
            int x = rcOwner.left + ((rcOwner.right - rcOwner.left) - 360) / 2;
            int y = rcOwner.top + ((rcOwner.bottom - rcOwner.top) - 140) / 2;
            w.SetWindowPos(nullptr, x, y, 360, 140, SWP_NOZORDER);
        } else {
            w.SetWindowPos(nullptr, 0, 0, 360, 140, SWP_NOMOVE | SWP_NOZORDER);
        }

        if (owner) ::EnableWindow(owner, FALSE);
        w.ShowWindow(SW_SHOW);
        w.m_edit.SetFocus();

        MSG msg;
        while (!w.m_done) {
            BOOL got = ::GetMessageW(&msg, nullptr, 0, 0);
            if (got == 0) {
                // WM_QUIT: foobar is shutting down. Put it back so the app's own
                // message loop still sees it, and abandon the prompt.
                ::PostQuitMessage(static_cast<int>(msg.wParam));
                break;
            }
            if (got == -1) break;   // message queue error
            if (!::IsDialogMessageW(w.m_hWnd, &msg)) {
                ::TranslateMessage(&msg);
                ::DispatchMessageW(&msg);
            }
        }
        if (owner) { ::EnableWindow(owner, TRUE); ::SetForegroundWindow(owner); }
        if (w.IsWindow()) w.DestroyWindow();

        out = w.m_value;
        return w.m_accepted;
    }

    BEGIN_MSG_MAP(TextPromptWindow)
        MSG_WM_CREATE(OnCreate)
        MSG_WM_CLOSE(OnClose)
        COMMAND_ID_HANDLER_EX(IDOK,     OnOk)
        COMMAND_ID_HANDLER_EX(IDCANCEL, OnCancel)
    END_MSG_MAP()

private:
    enum { IDC_PROMPT_LABEL = 4001, IDC_PROMPT_EDIT = 4002 };

    LRESULT OnCreate(LPCREATESTRUCT) {
        HFONT f = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        auto setFont = [&](HWND h) { SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(f), 0); };

        setFont(CreateWindowW(L"STATIC", m_label.c_str(), WS_CHILD | WS_VISIBLE,
            12, 12, 330, 18, *this,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PROMPT_LABEL)), nullptr, nullptr));

        m_edit.Create(*this, CWindow::rcDefault, nullptr,
            WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL,
            0, IDC_PROMPT_EDIT);
        m_edit.SetWindowPos(nullptr, 12, 34, 330, 22, SWP_NOZORDER);
        m_edit.SetFont(f);
        m_edit.SetWindowText(m_value.c_str());
        m_edit.SetSel(0, -1);

        // BS_DEFPUSHBUTTON is what makes IsDialogMessage translate Enter to IDOK.
        setFont(CreateWindowW(L"BUTTON", L"OK",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            180, 68, 76, 26, *this,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDOK)), nullptr, nullptr));
        setFont(CreateWindowW(L"BUTTON", L"Cancel",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            264, 68, 76, 26, *this,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDCANCEL)), nullptr, nullptr));
        return 0;
    }

    void OnOk(UINT, int, HWND) {
        int len = m_edit.GetWindowTextLength();
        std::wstring buf(static_cast<std::size_t>(len) + 1, L'\0');
        m_edit.GetWindowText(&buf[0], len + 1);
        buf.resize(static_cast<std::size_t>(len));
        m_value    = buf;
        m_accepted = true;
        m_done     = true;
    }

    void OnCancel(UINT, int, HWND) { m_done = true; }
    void OnClose()                 { m_done = true; }

    CEdit        m_edit;
    std::wstring m_label;
    std::wstring m_value;
    bool         m_accepted = false;
    bool         m_done     = false;
};

// Folder chooser for "Download Original Files". SHBrowseForFolder keeps this to
// one call with no COM object lifetime to manage.
bool pickFolder(HWND owner, std::wstring& outPath) {
    wchar_t display[MAX_PATH] = {};
    BROWSEINFOW bi = {};
    bi.hwndOwner      = owner;
    bi.pszDisplayName = display;
    bi.lpszTitle      = L"Choose a folder for the downloaded tracks";
    bi.ulFlags        = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    // BIF_NEWDIALOGSTYLE needs an initialized apartment. foobar's UI thread
    // already is one, so this normally returns S_FALSE; undo only what we did.
    HRESULT co = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    LPITEMIDLIST pidl = ::SHBrowseForFolderW(&bi);
    bool ok = false;
    if (pidl) {
        wchar_t path[MAX_PATH] = {};
        if (::SHGetPathFromIDListW(pidl, path)) { outPath = path; ok = true; }
        ::CoTaskMemFree(pidl);
    }
    if (SUCCEEDED(co)) ::CoUninitialize();
    return ok;
}

} // namespace

// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------
BrowserWindow& BrowserWindow::get() {
    static BrowserWindow inst;
    return inst;
}

void BrowserWindow::show() {
    if (!IsWindow()) {
        Create(nullptr, CWindow::rcDefault, L"Navidrome Browser",
               WS_OVERLAPPEDWINDOW, 0);
        SetWindowPos(nullptr, 0, 0, 580, 660,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_SHOWWINDOW);
        loadArtists();
    } else {
        ShowWindow(SW_SHOW);
        SetForegroundWindow(*this);
    }
}

// Inline mount for the Media Library prefs page. A fresh (non-singleton)
// instance owned by the host; the host sizes it to fill its client area.
void BrowserWindow::createEmbedded(HWND parent) {
    m_embedded = true;
    if (IsWindow()) return;
    RECT rc{}; ::GetClientRect(parent, &rc);
    Create(parent, rc, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0);
    loadArtists();
}

// ---------------------------------------------------------------------------
// Window messages
// ---------------------------------------------------------------------------
LRESULT BrowserWindow::OnCreate(LPCREATESTRUCT) {
    HFONT hFont = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

    // Search field
    m_search.Create(*this, CWindow::rcDefault, nullptr,
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 0, IDC_SEARCH);
    m_search.SetFont(hFont);
    m_search.SetCueBannerText(L"Search artists, albums, songs\u2026");

    // Tree view
    m_tree.Create(*this, CWindow::rcDefault, nullptr,
        WS_CHILD | WS_VISIBLE | WS_BORDER | TVS_HASLINES |
        TVS_LINESATROOT | TVS_HASBUTTONS | TVS_SHOWSELALWAYS,
        0, IDC_TREE);
    m_tree.SetFont(hFont);

    // Buttons
    m_addBtn.Create(*this, CWindow::rcDefault, L"Add to Playlist",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, IDC_ADD);
    m_addBtn.SetFont(hFont);

    m_playBtn.Create(*this, CWindow::rcDefault, L"Play Now",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, IDC_PLAY);
    m_playBtn.SetFont(hFont);

    m_refreshBtn.Create(*this, CWindow::rcDefault, L"Refresh",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, IDC_REFRESH);
    m_refreshBtn.SetFont(hFont);

    // Status label
    m_status.Create(*this, CWindow::rcDefault, nullptr,
        WS_CHILD | WS_VISIBLE | SS_LEFT, 0, IDC_STATUS);
    m_status.SetFont(hFont);

    // Follow foobar's Dark Mode preference (title bar on the standalone
    // window, control theming on both standalone and embedded mounts).
    m_darkMode.AddDialogWithControls(*this);
    // Follow foobar's Colours and Fonts scheme (e.g. the classic orange-on-
    // black look), independent of Dark Mode.
    refreshThemeColors();

    return 0;
}

// ---------------------------------------------------------------------------
// Colours and Fonts (Preferences > Display) sync
// ---------------------------------------------------------------------------
void BrowserWindow::refreshThemeColors() {
    m_theme = ThemeColors{};

    auto cfg = ui_config_manager::tryGet();
    if (cfg.is_valid()) {
        t_ui_color c = 0;
        if (cfg->query_color(ui_color_text, c))       { m_theme.textSet = true; m_theme.text = static_cast<COLORREF>(c); }
        if (cfg->query_color(ui_color_background, c)) { m_theme.bgSet   = true; m_theme.bg   = static_cast<COLORREF>(c); }
    }

    if (m_themeBgBrush) { ::DeleteObject(m_themeBgBrush); m_themeBgBrush = nullptr; }
    if (m_theme.bgSet) m_themeBgBrush = ::CreateSolidBrush(m_theme.bg);

    if (m_tree.IsWindow()) {
        m_tree.SetBkColor(m_theme.bgSet ? m_theme.bg : static_cast<COLORREF>(-1));
        m_tree.SetTextColor(m_theme.textSet ? m_theme.text : static_cast<COLORREF>(-1));
    }

    if (IsWindow()) {
        Invalidate();
        if (m_search.IsWindow()) m_search.Invalidate();
        if (m_status.IsWindow()) m_status.Invalidate();
    }
}

void BrowserWindow::ui_colors_changed() {
    refreshThemeColors();
}

BOOL BrowserWindow::OnEraseBkgnd(HDC dc) {
    if (!m_theme.bgSet) { SetMsgHandled(FALSE); return FALSE; }
    RECT rc; GetClientRect(&rc);
    ::FillRect(dc, &rc, m_themeBgBrush);
    return TRUE;
}

HBRUSH BrowserWindow::OnCtlColorEdit(HDC dc, HWND) {
    if (!m_theme.bgSet && !m_theme.textSet) { SetMsgHandled(FALSE); return nullptr; }
    if (m_theme.bgSet)   ::SetBkColor(dc, m_theme.bg);
    if (m_theme.textSet) ::SetTextColor(dc, m_theme.text);
    return m_theme.bgSet ? m_themeBgBrush : nullptr;
}

HBRUSH BrowserWindow::OnCtlColorStatic(HDC dc, HWND) {
    if (!m_theme.bgSet && !m_theme.textSet) { SetMsgHandled(FALSE); return nullptr; }
    if (m_theme.bgSet)   ::SetBkColor(dc, m_theme.bg);
    if (m_theme.textSet) ::SetTextColor(dc, m_theme.text);
    return m_theme.bgSet ? m_themeBgBrush : nullptr;
}

void BrowserWindow::OnDestroy() {
    m_nodeMap.clear();
    m_rootNodes.clear();
    if (m_themeBgBrush) { ::DeleteObject(m_themeBgBrush); m_themeBgBrush = nullptr; }
}

LRESULT BrowserWindow::OnSize(UINT, CSize sz) {
    const int pad = 6, btnH = 26, searchH = 22, statusW = 200;
    int w = sz.cx, h = sz.cy;

    m_search.SetWindowPos(nullptr,
        pad, pad, w - 2*pad, searchH,
        SWP_NOZORDER);
    m_tree.SetWindowPos(nullptr,
        pad, pad + searchH + pad,
        w - 2*pad, h - searchH - btnH - 4*pad,
        SWP_NOZORDER);

    int btnY = h - pad - btnH;
    int btnW = 110;
    m_refreshBtn.SetWindowPos(nullptr, pad, btnY, 80, btnH, SWP_NOZORDER);
    m_status.SetWindowPos(nullptr,
        pad + 80 + pad, btnY + 4,
        w - 80 - 2*btnW - 4*pad, btnH, SWP_NOZORDER);
    m_playBtn.SetWindowPos(nullptr,
        w - pad - btnW, btnY, btnW, btnH, SWP_NOZORDER);
    m_addBtn.SetWindowPos(nullptr,
        w - pad - 2*btnW - pad, btnY, btnW, btnH, SWP_NOZORDER);
    return 0;
}

// ---------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------
// Smart-list roots, shown above the artist list. They expand lazily like any
// other node, so opening the browser still costs exactly one getArtists call.
static std::vector<std::shared_ptr<NavidromeNode>> buildCategoryNodes() {
    struct { NavidromeNode::CategoryKind kind; const char* title; } kCategories[] = {
        { NavidromeNode::CatStarred,        "\u2605 Starred"   },
        { NavidromeNode::CatRecentlyAdded,  "Recently Added"   },
        { NavidromeNode::CatMostPlayed,     "Most Played"      },
        { NavidromeNode::CatRecentlyPlayed, "Recently Played"  },
        { NavidromeNode::CatRandom,         "Random Albums"    },
        { NavidromeNode::CatGenres,         "Genres"           },
        { NavidromeNode::CatPlaylists,      "Playlists"        },
        { NavidromeNode::CatBookmarks,      "Bookmarks"        },
    };

    std::vector<std::shared_ptr<NavidromeNode>> out;
    for (const auto& c : kCategories) {
        auto n = std::make_shared<NavidromeNode>();
        n->type        = NavidromeNode::Category;
        n->category    = c.kind;
        n->displayName = c.title;
        out.push_back(n);
    }
    return out;
}

void BrowserWindow::loadArtists() {
    setStatus("Loading artists\u2026");
    m_tree.DeleteAllItems();
    m_nodeMap.clear();
    m_rootNodes.clear();
    // Warm the cache the "Add to Navidrome Playlist" submenu reads from, so the
    // first right-click already lists the server's playlists.
    refreshServerPlaylists();

    std::thread([this]() {
        auto* payload = new LoadedPayload{};
        std::string err;
        auto artists = navidrome::SubsonicClientWin::get().getArtists(err);
        payload->error = err;
        if (err.empty()) {
            for (auto& n : buildCategoryNodes()) payload->nodes.push_back(n);
        }
        for (auto& a : artists) {
            auto n = std::make_shared<NavidromeNode>();
            n->type        = NavidromeNode::Artist;
            n->id          = a.id;
            n->displayName = a.name;
            n->coverArtId  = a.coverArtId;
            n->starred     = a.starred;
            payload->nodes.push_back(n);
        }
        PostMessage(WM_NAVIDROME_LOADED, reinterpret_cast<WPARAM>(payload), 0);
    }).detach();
}

// ---------------------------------------------------------------------------
// Child fetch (synchronous \u2014 background thread only)
// ---------------------------------------------------------------------------
std::vector<std::shared_ptr<NavidromeNode>>
BrowserWindow::fetchChildren(const std::shared_ptr<NavidromeNode>& node,
                             std::string& outError) {
    auto& client = navidrome::SubsonicClientWin::get();
    std::vector<std::shared_ptr<NavidromeNode>> out;

    auto addSong = [&out](const navidrome::Song& s, double bookmarkPositionMs = 0.0) {
        auto n = std::make_shared<NavidromeNode>();
        n->type           = NavidromeNode::Song;
        n->id             = s.id;
        n->displayName    = s.title;
        n->subtitle       = s.artist;
        n->album          = s.album;
        n->coverArtId     = s.coverArtId;
        n->suffix         = s.suffix;
        n->track          = s.track;
        n->year           = s.year;
        n->duration       = s.duration;
        n->starred        = s.starred;
        n->rating         = s.rating;
        n->bookmarkPositionMs = bookmarkPositionMs;
        n->childrenLoaded = true;
        out.push_back(n);
    };
    auto addAlbum = [&out](const navidrome::Album& a) {
        auto n = std::make_shared<NavidromeNode>();
        n->type        = NavidromeNode::Album;
        n->id          = a.id;
        n->displayName = a.name;
        n->subtitle    = a.artist;
        n->coverArtId  = a.coverArtId;
        n->starred     = a.starred;
        out.push_back(n);
    };

    switch (node->type) {
        case NavidromeNode::Artist:
            for (auto& a : client.getAlbumsForArtist(node->id, outError)) addAlbum(a);
            break;
        case NavidromeNode::Album:
            for (auto& s : client.getSongsForAlbum(node->id, outError)) addSong(s);
            break;
        case NavidromeNode::Playlist:
            for (auto& s : client.getPlaylistSongs(node->id, outError)) addSong(s);
            break;
        case NavidromeNode::Genre:
            // getSongsByGenre is paged; 500 covers all but the largest genres
            // and keeps a single request per expansion.
            for (auto& s : client.getSongsForGenre(node->id, 500, outError)) addSong(s);
            break;
        case NavidromeNode::Category:
            if (node->category == NavidromeNode::CatStarred) {
                for (auto& s : client.getStarredSongs(outError)) addSong(s);
            } else if (node->category == NavidromeNode::CatGenres) {
                for (auto& g : client.getGenres(outError)) {
                    auto n = std::make_shared<NavidromeNode>();
                    n->type        = NavidromeNode::Genre;
                    // getSongsByGenre keys off the name, not an id.
                    n->id          = g.name;
                    n->displayName = g.name;
                    n->subtitle    = std::to_string(g.songCount) +
                                     (g.songCount == 1 ? " track" : " tracks");
                    out.push_back(n);
                }
            } else if (node->category == NavidromeNode::CatPlaylists) {
                for (auto& p : client.getPlaylists(outError)) {
                    auto n = std::make_shared<NavidromeNode>();
                    n->type        = NavidromeNode::Playlist;
                    n->id          = p.id;
                    n->displayName = p.name;
                    n->subtitle    = std::to_string(p.songCount) +
                                     (p.songCount == 1 ? " track" : " tracks");
                    out.push_back(n);
                }
            } else if (node->category == NavidromeNode::CatBookmarks) {
                for (auto& b : client.getBookmarks(outError)) addSong(b.song, b.positionMs);
            } else {
                auto type = navidrome::AlbumListType::Newest;
                if (node->category == NavidromeNode::CatMostPlayed)
                    type = navidrome::AlbumListType::Frequent;
                else if (node->category == NavidromeNode::CatRecentlyPlayed)
                    type = navidrome::AlbumListType::Recent;
                else if (node->category == NavidromeNode::CatRandom)
                    type = navidrome::AlbumListType::Random;
                for (auto& a : client.getAlbumList(type, 100, outError)) addAlbum(a);
            }
            break;
        default:
            break;
    }

    if (!outError.empty()) out.clear();
    return out;
}

LRESULT BrowserWindow::OnNavidromeLoaded(UINT, WPARAM wParam, LPARAM, BOOL&) {
    auto* payload = reinterpret_cast<LoadedPayload*>(wParam);
    populateRoot(payload);
    delete payload;
    return 0;
}

LRESULT BrowserWindow::OnNavidromeChildren(UINT, WPARAM wParam, LPARAM, BOOL&) {
    auto* payload = reinterpret_cast<LoadedPayload*>(wParam);
    populateChildren(payload);
    delete payload;
    return 0;
}

LRESULT BrowserWindow::OnNavidromePlaylists(UINT, WPARAM wParam, LPARAM, BOOL&) {
    auto* lists = reinterpret_cast<std::vector<navidrome::Playlist>*>(wParam);
    m_playlistsLoading = false;
    if (lists) { m_serverPlaylists = std::move(*lists); delete lists; }
    return 0;
}

// Refresh the cached playlist list used by the "Add to Navidrome Playlist"
// submenu. Cheap enough to re-run after every mutation.
void BrowserWindow::refreshServerPlaylists() {
    if (m_playlistsLoading || !navidrome::SubsonicClientWin::get().isConfigured()) return;
    m_playlistsLoading = true;

    std::thread([this]() {
        std::string err;
        auto lists = navidrome::SubsonicClientWin::get().getPlaylists(err);
        // A null payload still gets posted on failure so the UI thread clears
        // m_playlistsLoading — and keeps the previous cache rather than blanking
        // the submenu over one bad request.
        auto* payload = err.empty()
            ? new std::vector<navidrome::Playlist>(std::move(lists))
            : nullptr;
        if (!PostMessage(WM_NAVIDROME_PLAYLISTS, reinterpret_cast<WPARAM>(payload), 0))
            delete payload;   // window already gone
    }).detach();
}

void BrowserWindow::populateRoot(LoadedPayload* payload) {
    if (!payload->error.empty()) {
        setStatus("Error: " + payload->error); return;
    }
    m_rootNodes = payload->nodes;
    std::size_t artists = 0, songs = 0;
    for (auto& n : m_rootNodes) {
        insertNode(TVI_ROOT, n);
        if (n->type == NavidromeNode::Artist) ++artists;
        if (n->type == NavidromeNode::Song)   ++songs;
    }
    // Search results arrive here too — they're songs, not artists.
    setStatus(songs > 0 ? std::to_string(songs) + " songs found"
                        : std::to_string(artists) + " artists");
}

void BrowserWindow::populateChildren(LoadedPayload* payload) {
    auto parent = payload->parent;
    if (!parent) return;

    // Remove placeholder "Loading..." item
    HTREEITEM hChild = m_tree.GetChildItem(parent->hItem);
    while (hChild) {
        HTREEITEM hNext = m_tree.GetNextSiblingItem(hChild);
        auto it = m_nodeMap.find(hChild);
        if (it != m_nodeMap.end() && it->second->type == NavidromeNode::Loading) {
            m_tree.DeleteItem(hChild);
            m_nodeMap.erase(it);
        }
        hChild = hNext;
    }

    parent->isLoading      = false;
    parent->childrenLoaded = true;
    parent->children       = payload->nodes;

    if (!payload->error.empty()) {
        auto errNode = std::make_shared<NavidromeNode>();
        errNode->type        = NavidromeNode::Error;
        errNode->displayName = "Error: " + payload->error;
        parent->children     = { errNode };
    }

    for (auto& child : parent->children)
        insertNode(parent->hItem, child);

    if (parent->children.empty()) {
        // No children — clear the expand button. WTL's CTreeViewCtrl has no
        // SetItemChildren; set cChildren via the TVITEM mask directly.
        TVITEM it   = {};
        it.mask     = TVIF_CHILDREN;
        it.hItem    = parent->hItem;
        it.cChildren = 0;
        m_tree.SetItem(&it);
    }
}

// Tree label: track number, favorite marker and rating stars all live in the
// item text — a treeview has no extra columns to put them in.
std::string BrowserWindow::labelFor(const std::shared_ptr<NavidromeNode>& node) const {
    std::string label = node->displayName;
    if (node->type == NavidromeNode::Song && node->track > 0)
        label = std::to_string(node->track) + ". " + label;
    // Category rows carry their own icon in the title.
    if (node->starred && node->type != NavidromeNode::Category)
        label = "★ " + label;
    if (node->rating > 0) {
        label += "  ";
        for (int i = 0; i < node->rating; ++i) label += "★";
    }
    if (node->bookmarkPositionMs > 0) {
        int totalSeconds = static_cast<int>(node->bookmarkPositionMs / 1000.0);
        char buf[16];
        snprintf(buf, sizeof(buf), "  ⏱ %d:%02d", totalSeconds / 60, totalSeconds % 60);
        label += buf;
    }
    return label;
}

void BrowserWindow::refreshLabel(const std::shared_ptr<NavidromeNode>& node) {
    if (!node || !node->hItem) return;
    m_tree.SetItemText(node->hItem, u8ToWide(labelFor(node)).c_str());
}

HTREEITEM BrowserWindow::insertNode(HTREEITEM hParent,
                                    std::shared_ptr<NavidromeNode> node) {
    std::string label = labelFor(node);

    TVINSERTSTRUCT tvi    = {};
    tvi.hParent           = hParent;
    tvi.hInsertAfter      = TVI_LAST;
    tvi.item.mask         = TVIF_TEXT | TVIF_PARAM | TVIF_CHILDREN;
    auto wlabel           = u8ToWide(label);
    tvi.item.pszText      = const_cast<LPWSTR>(wlabel.c_str());
    tvi.item.lParam       = reinterpret_cast<LPARAM>(node.get());
    // Show expand arrow for artists and albums
    tvi.item.cChildren    = (node->type == NavidromeNode::Song   ||
                              node->type == NavidromeNode::Error  ||
                              node->type == NavidromeNode::Loading) ? 0 : 1;

    HTREEITEM hItem = m_tree.InsertItem(&tvi);
    node->hItem = hItem;
    m_nodeMap[hItem] = node;
    return hItem;
}

std::shared_ptr<NavidromeNode> BrowserWindow::nodeForItem(HTREEITEM hItem) {
    auto it = m_nodeMap.find(hItem);
    return (it != m_nodeMap.end()) ? it->second : nullptr;
}

// ---------------------------------------------------------------------------
// Tree events
// ---------------------------------------------------------------------------
LRESULT BrowserWindow::OnTreeExpanding(LPNMHDR pnmh) {
    auto* pnm = reinterpret_cast<LPNMTREEVIEW>(pnmh);
    if (pnm->action != TVE_EXPAND) return 0;

    auto node = nodeForItem(pnm->itemNew.hItem);
    if (!node || node->childrenLoaded || node->isLoading) return 0;
    node->isLoading = true;

    // Insert placeholder
    auto loadNode = std::make_shared<NavidromeNode>();
    loadNode->type        = NavidromeNode::Loading;
    loadNode->displayName = "Loading\u2026";
    insertNode(node->hItem, loadNode);

    std::thread([this, node]() {
        auto* payload  = new LoadedPayload{};
        payload->parent = node;
        std::string err;
        payload->nodes = fetchChildren(node, err);
        payload->error = err;
        PostMessage(WM_NAVIDROME_CHILDREN,
                    reinterpret_cast<WPARAM>(payload), 0);
    }).detach();

    return 0;
}

LRESULT BrowserWindow::OnTreeDblClick(LPNMHDR) {
    HTREEITEM hSel = m_tree.GetSelectedItem();
    if (!hSel) return 0;
    auto node = nodeForItem(hSel);
    if (!node) return 0;
    if (node->type == NavidromeNode::Song)
        enqueueNodes({ node }, true);
    else if (m_tree.GetItemState(hSel, TVIS_EXPANDED) & TVIS_EXPANDED)
        m_tree.Expand(hSel, TVE_COLLAPSE);
    else
        m_tree.Expand(hSel, TVE_EXPAND);
    return 0;
}

// ---------------------------------------------------------------------------
// Button actions
// ---------------------------------------------------------------------------
// Gather the tree's selected, playable nodes (standard treeview is single-
// select, but iterating TVIS_SELECTED keeps this correct if that ever changes).
std::vector<std::shared_ptr<NavidromeNode>> BrowserWindow::selectedNodes() {
    std::vector<std::shared_ptr<NavidromeNode>> selected;
    HTREEITEM hItem = m_tree.GetFirstVisibleItem();
    while (hItem) {
        if (m_tree.GetItemState(hItem, TVIS_SELECTED) & TVIS_SELECTED) {
            auto n = nodeForItem(hItem);
            if (n && n->type != NavidromeNode::Loading && n->type != NavidromeNode::Error)
                selected.push_back(n);
        }
        hItem = m_tree.GetNextVisibleItem(hItem);
    }
    return selected;
}

// Resolve the selected nodes to songs on a background thread, then enqueue on
// the main thread. closeAfter hides the window once the tracks are queued \u2014
// used by the Enter shortcut so "select artist + Enter" queues and dismisses.
void BrowserWindow::queueSelected(bool play, bool closeAfter, bool clearFirst) {
    auto selected = selectedNodes();
    if (selected.empty()) { setStatus("Select at least one item"); return; }

    setStatus("Loading tracks\u2026");
    std::thread([this, selected, play, closeAfter, clearFirst]() {
        std::vector<std::shared_ptr<NavidromeNode>> songs;
        for (auto& n : selected)
            collectSongsDeep(n, songs);
        fb2k::inMainThread([this, songs, play, closeAfter, clearFirst]() mutable {
            enqueueNodes(std::move(songs), play, clearFirst);
            if (closeAfter && !m_embedded && IsWindow()) ShowWindow(SW_HIDE);
        });
    }).detach();
}

void BrowserWindow::OnAdd(UINT, int, HWND)  { queueSelected(false, false); }
void BrowserWindow::OnPlay(UINT, int, HWND) { queueSelected(true,  false); }

// Enter in the tree = replace the active playlist with the selected item(s),
// start playing, and close the window. A quick "jump to this artist" shortcut.
LRESULT BrowserWindow::OnTreeReturn(LPNMHDR) {
    queueSelected(true, true, true);
    return 0;
}

// Right-click context menu on the tree — mirrors the Add/Play buttons for a
// native feel. The menu item IDs are IDC_PLAY / IDC_ADD, so TrackPopupMenu
// posts WM_COMMAND straight into the existing OnPlay / OnAdd handlers.
void BrowserWindow::OnContextMenu(CWindow wnd, CPoint point) {
    if (wnd.m_hWnd != m_tree.m_hWnd) { SetMsgHandled(FALSE); return; }

    if (point.x == -1 && point.y == -1) {
        // Keyboard-invoked (Shift+F10 / menu key): anchor on the selected item.
        HTREEITEM sel = m_tree.GetSelectedItem();
        CRect rc;
        if (sel && m_tree.GetItemRect(sel, &rc, TRUE)) point = rc.CenterPoint();
        else { m_tree.GetClientRect(&rc); point = rc.TopLeft(); }
        m_tree.ClientToScreen(&point);
    } else {
        // Mouse: select the row under the cursor so the action targets it.
        CPoint client(point);
        m_tree.ScreenToClient(&client);
        UINT flags = 0;
        HTREEITEM hit = m_tree.HitTest(client, &flags);
        if (hit) m_tree.SelectItem(hit);
    }

    if (selectedNodes().empty()) return;

    CMenu menu;
    menu.CreatePopupMenu();
    menu.AppendMenu(MF_STRING, IDC_PLAY, L"Play Now");
    menu.AppendMenu(MF_STRING, IDC_ADD,  L"Add to Playlist");

    // Server-side favorites + ratings. Both are per-user state on Navidrome, so
    // they show up in its web UI and in every other Subsonic client.
    menu.AppendMenu(MF_SEPARATOR);
    menu.AppendMenu(MF_STRING, IDC_STAR,   L"Star");
    menu.AppendMenu(MF_STRING, IDC_UNSTAR, L"Unstar");
    menu.AppendMenu(MF_STRING, IDC_REMOVE_BOOKMARK, L"Remove Bookmark");

    CMenu rating;
    rating.CreatePopupMenu();
    rating.AppendMenu(MF_STRING, IDC_RATE_0, L"None");
    static const wchar_t* kStars[] = { L"★", L"★★", L"★★★", L"★★★★", L"★★★★★" };
    for (int i = 0; i < 5; ++i)
        rating.AppendMenu(MF_STRING, IDC_RATE_0 + 1 + i, kStars[i]);
    menu.AppendMenu(MF_POPUP, reinterpret_cast<UINT_PTR>(rating.m_hMenu), L"Rating");
    // The parent menu owns the submenu now; detach so CMenu's destructor
    // doesn't destroy it out from under TrackPopupMenu.
    rating.Detach();

    // Server playlists. The submenu is built from the cached list, so opening
    // the menu never blocks on the network.
    menu.AppendMenu(MF_SEPARATOR);
    CMenu playlists;
    playlists.CreatePopupMenu();
    const std::size_t shown = (std::min)(m_serverPlaylists.size(), kMaxPlaylistMenuEntries);
    for (std::size_t i = 0; i < shown; ++i) {
        playlists.AppendMenu(MF_STRING,
            static_cast<UINT_PTR>(IDC_PLAYLIST_FIRST + i),
            u8ToWide(m_serverPlaylists[i].name).c_str());
    }
    if (shown == 0) {
        playlists.AppendMenu(MF_STRING | MF_GRAYED, static_cast<UINT_PTR>(0),
            m_playlistsLoading ? L"Loading…" : L"No playlists on server");
    }
    playlists.AppendMenu(MF_SEPARATOR);
    playlists.AppendMenu(MF_STRING, IDC_NEW_PLAYLIST, L"New Playlist…");
    menu.AppendMenu(MF_POPUP, reinterpret_cast<UINT_PTR>(playlists.m_hMenu),
                    L"Add to Navidrome Playlist");
    // The parent menu owns the submenu now; detach so CMenu's destructor doesn't
    // destroy it out from under TrackPopupMenu.
    playlists.Detach();

    menu.AppendMenu(MF_STRING, IDC_REMOVE_FROM_PL,  L"Remove from Playlist");
    menu.AppendMenu(MF_STRING, IDC_RENAME_PLAYLIST, L"Rename Playlist…");
    menu.AppendMenu(MF_STRING, IDC_DELETE_PLAYLIST, L"Delete Playlist…");

    menu.AppendMenu(MF_SEPARATOR);
    menu.AppendMenu(MF_STRING, IDC_SEND_PLAYLIST,
                    L"Send Active Playlist to Navidrome");
    menu.AppendMenu(MF_STRING, IDC_DOWNLOAD, L"Download Original Files…");

    menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y, *this);

    // A stale cache is only visible once — refresh for the next open.
    refreshServerPlaylists();
}

// ---------------------------------------------------------------------------
// Favorites, ratings and playlist upload
// ---------------------------------------------------------------------------
void BrowserWindow::OnStar(UINT, int, HWND)   { applyStarred(true); }
void BrowserWindow::OnUnstar(UINT, int, HWND) { applyStarred(false); }

void BrowserWindow::applyStarred(bool starred) {
    std::vector<std::shared_ptr<NavidromeNode>> targets;
    for (auto& n : selectedNodes()) {
        if (n->type == NavidromeNode::Song ||
            n->type == NavidromeNode::Album ||
            n->type == NavidromeNode::Artist)
            targets.push_back(n);
    }
    if (targets.empty()) { setStatus("Select a song, album or artist first"); return; }

    std::thread([this, targets, starred]() {
        std::string err;
        std::size_t done = 0;
        for (auto& n : targets) {
            navidrome::StarKind kind = navidrome::StarKind::Song;
            if (n->type == NavidromeNode::Album)  kind = navidrome::StarKind::Album;
            if (n->type == NavidromeNode::Artist) kind = navidrome::StarKind::Artist;

            std::string one;
            if (navidrome::SubsonicClientWin::get().setStarred(starred, n->id, kind, one)) {
                n->starred = starred;
                ++done;
            } else if (err.empty()) {
                err = one;
            }
        }
        fb2k::inMainThread([this, targets, starred, done, err]() {
            if (!IsWindow()) return;
            for (auto& n : targets) refreshLabel(n);
            setStatus(err.empty()
                ? (starred ? "Starred " : "Unstarred ") + std::to_string(done) + " item(s)"
                : "Error: " + err);
        });
    }).detach();
}

void BrowserWindow::OnRemoveBookmark(UINT, int, HWND) { applyRemoveBookmark(); }

void BrowserWindow::applyRemoveBookmark() {
    std::vector<std::shared_ptr<NavidromeNode>> songs;
    for (auto& n : selectedNodes())
        if (n->type == NavidromeNode::Song) songs.push_back(n);
    if (songs.empty()) { setStatus("Select one or more songs"); return; }

    std::thread([this, songs]() {
        std::string err;
        std::size_t done = 0;
        for (auto& n : songs) {
            std::string one;
            if (navidrome::SubsonicClientWin::get().deleteBookmark(n->id, one)) {
                n->bookmarkPositionMs = 0.0;
                ++done;
            } else if (err.empty()) {
                err = one;
            }
        }
        fb2k::inMainThread([this, songs, done, err]() {
            if (!IsWindow()) return;
            for (auto& n : songs) refreshLabel(n);
            invalidateBookmarksCategory();
            setStatus(err.empty()
                ? "Removed " + std::to_string(done) + " bookmark(s)"
                : "Error: " + err);
        });
    }).detach();
}

// Ratings are a song-level concept in Subsonic; albums/artists are ignored.
void BrowserWindow::OnRate(UINT, int id, HWND) {
    int stars = id - IDC_RATE_0;
    if (stars < 0 || stars > 5) return;

    std::vector<std::shared_ptr<NavidromeNode>> songs;
    for (auto& n : selectedNodes())
        if (n->type == NavidromeNode::Song) songs.push_back(n);
    if (songs.empty()) { setStatus("Select one or more songs to rate"); return; }

    std::thread([this, songs, stars]() {
        std::string err;
        for (auto& n : songs) {
            std::string one;
            if (navidrome::SubsonicClientWin::get().setRating(stars, n->id, one))
                n->rating = stars;
            else if (err.empty())
                err = one;
        }
        fb2k::inMainThread([this, songs, err]() {
            if (!IsWindow()) return;
            for (auto& n : songs) refreshLabel(n);
            setStatus(err.empty()
                ? "Rated " + std::to_string(songs.size()) + " song(s)"
                : "Error: " + err);
        });
    }).detach();
}

// Pushes the active foobar2000 playlist to the server under the same name, so
// it shows up on phones / the web UI. Only navidrome:// tracks can be sent —
// local files have no Subsonic id.
void BrowserWindow::OnSendActivePlaylist(UINT, int, HWND) {
    auto pm = playlist_manager::get();
    t_size pl = pm->get_active_playlist();
    if (pl == pfc_infinite) { setStatus("No active playlist"); return; }

    pfc::string8 pfcName;
    pm->playlist_get_name(pl, pfcName);
    metadb_handle_list items;
    pm->playlist_get_all_items(pl, items);

    std::vector<std::string> songIds;
    std::size_t skipped = 0;
    for (t_size i = 0; i < items.get_count(); ++i) {
        std::string id = navidrome::trackIdFromURI(items[i]->get_path());
        if (id.empty()) { ++skipped; continue; }
        songIds.push_back(id);
    }
    if (songIds.empty()) {
        setStatus("No Navidrome tracks in the active playlist");
        return;
    }

    std::string name = pfcName.is_empty() ? "foobar2000" : pfcName.c_str();
    setStatus("Uploading playlist…");

    std::thread([this, name, songIds, skipped]() {
        std::string err;
        navidrome::SubsonicClientWin::get().createPlaylist(name, songIds, err);
        // The id is only needed to grow the playlist further; an empty id with
        // no error still means the upload succeeded.
        bool ok = err.empty();
        fb2k::inMainThread([this, name, songIds, skipped, ok, err]() {
            if (!IsWindow()) return;
            if (!ok) {
                setStatus("Upload failed: " + err);
                return;
            }
            std::string msg = "Sent \"" + name + "\" (" +
                              std::to_string(songIds.size()) + " tracks";
            if (skipped > 0)
                msg += ", " + std::to_string(skipped) + " non-Navidrome skipped";
            setStatus(msg + ")");
            invalidatePlaylistsCategory();
            refreshServerPlaylists();
        });
    }).detach();
}

// ---------------------------------------------------------------------------
// Download originals
//
// download.view always serves the file as stored on the server — the streaming
// transcode preferences deliberately don't apply here.
// ---------------------------------------------------------------------------
void BrowserWindow::OnDownload(UINT, int, HWND) {
    auto selected = selectedNodes();
    if (selected.empty()) { setStatus("Select at least one item"); return; }

    std::wstring destDir;
    if (!pickFolder(*this, destDir)) return;

    setStatus("Resolving tracks…");
    std::thread([this, destDir, selected]() {
        std::vector<std::shared_ptr<NavidromeNode>> songs;
        for (auto& n : selected) collectSongsDeep(n, songs);

        std::size_t done = 0, failed = 0;
        for (std::size_t i = 0; i < songs.size(); ++i) {
            const std::size_t position = i + 1, total = songs.size();
            fb2k::inMainThread([this, position, total]() {
                if (IsWindow())
                    setStatus("Downloading " + std::to_string(position) + "/" +
                              std::to_string(total) + "…");
            });

            auto& s = songs[i];
            // "<track>. <artist> - <title>.<suffix>"
            std::string name;
            if (s->track > 0) {
                char buf[8];
                snprintf(buf, sizeof(buf), "%02d. ", s->track);
                name += buf;
            }
            if (!s->subtitle.empty()) name += s->subtitle + " - ";
            name += s->displayName.empty() ? "untitled" : s->displayName;
            name = navidrome::sanitizeFileName(name);
            if (!s->suffix.empty()) name += "." + s->suffix;

            std::string err;
            std::string url = navidrome::SubsonicClientWin::get().downloadURL(s->id);
            if (navidrome::SubsonicClientWin::get()
                    .httpDownloadToFile(url, destDir + L"\\" + u8ToWide(name), err))
                ++done;
            else
                ++failed;
        }

        fb2k::inMainThread([this, done, failed]() {
            if (!IsWindow()) return;
            setStatus(failed == 0
                ? "Downloaded " + std::to_string(done) + " track(s)"
                : "Downloaded " + std::to_string(done) + ", " +
                  std::to_string(failed) + " failed");
        });
    }).detach();
}

// ---------------------------------------------------------------------------
// Server playlist management
//
// Everything here works on song ids, so the selection is first resolved down to
// songs the same way the Add/Play actions resolve it.
// ---------------------------------------------------------------------------

// Synchronous — call from a background thread (collectSongsDeep hits the API for
// nodes that haven't been expanded yet). `nodes` must have been captured on the
// UI thread; reading the tree control from here would be a cross-thread call.
std::vector<std::string> BrowserWindow::collectSongIdsDeep(
        const std::vector<std::shared_ptr<NavidromeNode>>& nodes) {
    std::vector<std::shared_ptr<NavidromeNode>> songs;
    for (auto& n : nodes) collectSongsDeep(n, songs);

    std::vector<std::string> ids;
    ids.reserve(songs.size());
    for (auto& s : songs)
        if (!s->id.empty()) ids.push_back(s->id);
    return ids;
}

std::shared_ptr<NavidromeNode> BrowserWindow::singleSelectedPlaylist() {
    auto sel = selectedNodes();
    if (sel.size() != 1 || sel[0]->type != NavidromeNode::Playlist) return nullptr;
    return sel[0];
}

// Drop a node's cached children (and their tree items) so the next expand
// refetches from the server.
void BrowserWindow::reloadNodeChildren(const std::shared_ptr<NavidromeNode>& node) {
    if (!node || !node->hItem) return;

    m_tree.Expand(node->hItem, TVE_COLLAPSE);
    HTREEITEM child = m_tree.GetChildItem(node->hItem);
    while (child) {
        HTREEITEM next = m_tree.GetNextSiblingItem(child);
        m_nodeMap.erase(child);
        m_tree.DeleteItem(child);
        child = next;
    }
    node->children.clear();
    node->childrenLoaded = false;
    node->isLoading      = false;

    // Restore the expand arrow the delete may have cleared.
    TVITEM it    = {};
    it.mask      = TVIF_CHILDREN;
    it.hItem     = node->hItem;
    it.cChildren = 1;
    m_tree.SetItem(&it);
}

void BrowserWindow::invalidatePlaylistNode(const std::string& playlistId) {
    for (auto& root : m_rootNodes) {
        if (root->type != NavidromeNode::Category ||
            root->category != NavidromeNode::CatPlaylists) continue;
        for (auto& pl : root->children) {
            if (pl->id != playlistId) continue;
            reloadNodeChildren(pl);
            return;
        }
    }
}

void BrowserWindow::invalidatePlaylistsCategory() {
    for (auto& root : m_rootNodes) {
        if (root->type == NavidromeNode::Category &&
            root->category == NavidromeNode::CatPlaylists) {
            reloadNodeChildren(root);
            return;
        }
    }
}

void BrowserWindow::invalidateBookmarksCategory() {
    for (auto& root : m_rootNodes) {
        if (root->type == NavidromeNode::Category &&
            root->category == NavidromeNode::CatBookmarks) {
            reloadNodeChildren(root);
            return;
        }
    }
}

// playback_control::start() has just been called; the stream isn't necessarily
// seekable the instant it begins decoding, so poll briefly before giving up.
void BrowserWindow::seekWhenReady(double positionSeconds) {
    std::thread([positionSeconds]() {
        auto pc = playback_control::get();
        for (int i = 0; i < 30; ++i) {
            if (pc->is_playing() && pc->playback_can_seek()) {
                fb2k::inMainThread([positionSeconds]() {
                    playback_control::get()->playback_seek(positionSeconds);
                });
                return;
            }
            Sleep(100);
        }
    }).detach();
}

void BrowserWindow::OnAddToServerPlaylist(UINT, int id, HWND) {
    const std::size_t idx = static_cast<std::size_t>(id - IDC_PLAYLIST_FIRST);
    if (idx >= m_serverPlaylists.size()) return;
    const std::string playlistId = m_serverPlaylists[idx].id;
    const std::string name       = m_serverPlaylists[idx].name;

    auto selected = selectedNodes();
    if (selected.empty()) { setStatus("Select at least one item"); return; }

    setStatus("Resolving tracks…");
    std::thread([this, playlistId, name, selected]() {
        auto ids = collectSongIdsDeep(selected);
        if (ids.empty()) {
            fb2k::inMainThread([this]() {
                if (IsWindow()) setStatus("No tracks in the selection");
            });
            return;
        }
        std::string err;
        bool ok = navidrome::SubsonicClientWin::get().addToPlaylist(playlistId, ids, err);
        fb2k::inMainThread([this, playlistId, name, ids, ok, err]() {
            if (!IsWindow()) return;
            setStatus(ok ? "Added " + std::to_string(ids.size()) + " track(s) to \"" + name + "\""
                         : "Failed: " + (err.empty() ? "unknown error" : err));
            if (ok) { invalidatePlaylistNode(playlistId); refreshServerPlaylists(); }
        });
    }).detach();
}

void BrowserWindow::OnNewServerPlaylist(UINT, int, HWND) {
    auto selected = selectedNodes();
    if (selected.empty()) { setStatus("Select at least one item"); return; }

    std::wstring name;
    if (!TextPromptWindow::run(*this, L"New Navidrome playlist",
                               L"Name for the new playlist:", L"", name))
        return;
    std::string nameU8 = wToU8(name);
    if (nameU8.empty()) return;

    setStatus("Resolving tracks…");
    std::thread([this, nameU8, selected]() {
        auto ids = collectSongIdsDeep(selected);
        std::string err;
        std::string newId =
            navidrome::SubsonicClientWin::get().createPlaylist(nameU8, ids, err);
        // An empty id with no error means the server just didn't echo one back.
        bool ok = err.empty();
        fb2k::inMainThread([this, nameU8, ids, ok, err]() {
            if (!IsWindow()) return;
            setStatus(ok ? "Created \"" + nameU8 + "\" (" +
                           std::to_string(ids.size()) + " track(s))"
                         : "Failed: " + err);
            if (ok) { invalidatePlaylistsCategory(); refreshServerPlaylists(); }
        });
    }).detach();
}

// Only meaningful for song rows sitting directly under a playlist node — that's
// where a track has a position for songIndexToRemove to refer to.
void BrowserWindow::OnRemoveFromPlaylist(UINT, int, HWND) {
    std::shared_ptr<NavidromeNode> playlist;
    std::vector<int> indexes;

    for (auto& n : selectedNodes()) {
        if (n->type != NavidromeNode::Song || !n->hItem) continue;
        auto parent = nodeForItem(m_tree.GetParentItem(n->hItem));
        if (!parent || parent->type != NavidromeNode::Playlist) continue;
        // Mixing playlists in one request isn't expressible — the endpoint takes
        // a single playlistId.
        if (playlist && playlist->id != parent->id) continue;
        playlist = parent;
        for (std::size_t i = 0; i < parent->children.size(); ++i) {
            if (parent->children[i] == n) { indexes.push_back(static_cast<int>(i)); break; }
        }
    }

    if (!playlist || indexes.empty()) {
        setStatus("Select tracks inside a server playlist first");
        return;
    }

    const std::string playlistId = playlist->id;
    const std::string name       = playlist->displayName;
    setStatus("Removing…");
    std::thread([this, playlistId, name, indexes]() {
        std::string err;
        bool ok = navidrome::SubsonicClientWin::get()
                      .removeFromPlaylist(playlistId, indexes, err);
        fb2k::inMainThread([this, playlistId, name, indexes, ok, err]() {
            if (!IsWindow()) return;
            setStatus(ok ? "Removed " + std::to_string(indexes.size()) +
                           " track(s) from \"" + name + "\""
                         : "Failed: " + (err.empty() ? "unknown error" : err));
            if (ok) invalidatePlaylistNode(playlistId);
        });
    }).detach();
}

void BrowserWindow::OnRenamePlaylist(UINT, int, HWND) {
    auto playlist = singleSelectedPlaylist();
    if (!playlist) { setStatus("Select a single server playlist"); return; }

    std::wstring name;
    if (!TextPromptWindow::run(*this, L"Rename playlist", L"New name:",
                               u8ToWide(playlist->displayName), name))
        return;
    std::string nameU8 = wToU8(name);
    if (nameU8.empty() || nameU8 == playlist->displayName) return;

    const std::string playlistId = playlist->id;
    std::thread([this, playlist, playlistId, nameU8]() {
        std::string err;
        bool ok = navidrome::SubsonicClientWin::get()
                      .renamePlaylist(playlistId, nameU8, err);
        fb2k::inMainThread([this, playlist, nameU8, ok, err]() {
            if (!IsWindow()) return;
            if (ok) {
                playlist->displayName = nameU8;
                refreshLabel(playlist);
                setStatus("Renamed to \"" + nameU8 + "\"");
                refreshServerPlaylists();
            } else {
                setStatus("Failed: " + (err.empty() ? "unknown error" : err));
            }
        });
    }).detach();
}

void BrowserWindow::OnDeletePlaylist(UINT, int, HWND) {
    auto playlist = singleSelectedPlaylist();
    if (!playlist) { setStatus("Select a single server playlist"); return; }

    std::wstring prompt = L"Delete \"" + u8ToWide(playlist->displayName) +
                          L"\" from the server?\r\n\r\n"
                          L"The playlist is removed for every client. "
                          L"The tracks themselves are not touched.";
    if (MessageBoxW(prompt.c_str(), L"Delete playlist",
                    MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2) != IDYES)
        return;

    const std::string playlistId = playlist->id;
    const std::string name       = playlist->displayName;
    std::thread([this, playlistId, name]() {
        std::string err;
        bool ok = navidrome::SubsonicClientWin::get().deletePlaylist(playlistId, err);
        fb2k::inMainThread([this, name, ok, err]() {
            if (!IsWindow()) return;
            setStatus(ok ? "Deleted \"" + name + "\""
                         : "Failed: " + (err.empty() ? "unknown error" : err));
            if (ok) { invalidatePlaylistsCategory(); refreshServerPlaylists(); }
        });
    }).detach();
}

void BrowserWindow::OnRefresh(UINT, int, HWND) {
    m_search.SetWindowText(L"");
    loadArtists();
}

void BrowserWindow::OnSearchChanged(UINT, int, HWND) {
    wchar_t buf[256] = {};
    m_search.GetWindowText(buf, 256);
    std::string query = wToU8(buf);
    if (query.size() < 2) {
        if (m_rootNodes.empty()) loadArtists();
        return;
    }
    setStatus("Searching\u2026");
    std::thread([this, query]() {
        std::string err;
        auto results = navidrome::SubsonicClientWin::get().search(query, err);
        auto* payload = new LoadedPayload{};
        payload->error = err;
        for (auto& s : results.songs) {
            auto n = std::make_shared<NavidromeNode>();
            n->type           = NavidromeNode::Song;
            n->id             = s.id;
            n->displayName    = s.title + " — " + s.artist;
            n->subtitle       = s.artist;
            n->album          = s.album;
            n->coverArtId     = s.coverArtId;
            n->suffix         = s.suffix;
            n->track          = s.track;
            n->year           = s.year;
            n->duration       = s.duration;
            n->starred        = s.starred;
            n->rating         = s.rating;
            n->childrenLoaded = true;
            payload->nodes.push_back(n);
        }
        PostMessage(WM_NAVIDROME_LOADED, reinterpret_cast<WPARAM>(payload), 0);
    }).detach();
}

// ---------------------------------------------------------------------------
// Deep song collection (synchronous, call from background thread)
// ---------------------------------------------------------------------------
// Walks any expandable node (artist, album, category, playlist) down to songs,
// reusing already-expanded children and fetching the rest on demand.
void BrowserWindow::collectSongsDeep(std::shared_ptr<NavidromeNode> node,
                                     std::vector<std::shared_ptr<NavidromeNode>>& out) {
    if (node->type == NavidromeNode::Song) { out.push_back(node); return; }
    if (node->type == NavidromeNode::Loading || node->type == NavidromeNode::Error) return;

    if (node->childrenLoaded && !node->children.empty()) {
        for (auto& c : node->children) collectSongsDeep(c, out);
        return;
    }

    std::string err;
    for (auto& c : fetchChildren(node, err)) collectSongsDeep(c, out);
}

// ---------------------------------------------------------------------------
// Enqueue to foobar2000 playlist (call from main thread)
// ---------------------------------------------------------------------------
void BrowserWindow::enqueueNodes(std::vector<std::shared_ptr<NavidromeNode>> songs,
                                 bool play, bool clearFirst) {
    if (songs.empty()) { setStatus("No songs selected"); return; }

    metadb_handle_list tracks;
    auto hints = metadb_io_v2::get()->create_hint_list();

    for (auto& node : songs) {
        // Enqueue a navidrome://track/<id>?... URI (not the raw HTTP URL) so the
        // input handler resolves the stream — with custom headers — at decode
        // time, and metadata renders without a network round-trip.
        std::string uri = navidrome::makeTrackURI(node->id, node->displayName,
            node->subtitle, node->album, node->track, node->year,
            node->duration, node->coverArtId, node->suffix);
        if (uri.empty()) continue;

        metadb_handle_ptr handle;
        playable_location_impl loc;
        loc.set_path(uri.c_str());
        loc.set_subsong(0);
        metadb::get()->handle_create(handle, loc);
        tracks += handle;

        file_info_impl info;
        if (!node->displayName.empty()) info.meta_set("title",  node->displayName.c_str());
        if (!node->subtitle.empty())    info.meta_set("artist", node->subtitle.c_str());
        if (!node->album.empty())       info.meta_set("album",  node->album.c_str());
        if (node->track > 0)            info.meta_set("tracknumber", pfc::format_int(node->track));
        if (node->year > 0)             info.meta_set("date",   pfc::format_int(node->year));
        if (node->duration > 0)         info.set_length(node->duration);
        hints->add_hint(handle, info, filestats_invalid, true);
    }
    hints->on_done();

    auto pm = playlist_manager::get();
    t_size pl = pm->get_active_playlist();
    if (pl == pfc_infinite) {
        pm->create_playlist("Navidrome", ~0, pfc_infinite);
        pl = pm->get_active_playlist();
    }
    if (clearFirst) pm->playlist_clear(pl);
    t_size insertPos = pm->playlist_get_item_count(pl);
    pm->playlist_add_items(pl, tracks, pfc::bit_array_false());

    if (play && tracks.get_count() > 0) {
        // Start playback honoring the user's Playback > Order setting (Shuffle,
        // Random, Default, …). track_command_play asks the active playback order
        // for the starting track; the focus biases in-order modes to the first
        // newly-added track. (playlist_execute_default_action would instead pin
        // that exact track and ignore the order.)
        pm->set_active_playlist(pl);
        pm->set_playing_playlist(pl);
        pm->playlist_set_focus_item(pl, insertPos);
        playback_control::get()->start(playback_control::track_command_play);

        // Resume a saved position when this was a single bookmarked song.
        if (songs.size() == 1 && songs[0]->bookmarkPositionMs > 0)
            seekWhenReady(songs[0]->bookmarkPositionMs / 1000.0);
    }

    std::string msg = "Added " + std::to_string(tracks.get_count()) + " tracks";
    setStatus(msg);
}

void BrowserWindow::setStatus(const std::string& msg) {
    m_status.SetWindowText(u8ToWide(msg).c_str());
}
