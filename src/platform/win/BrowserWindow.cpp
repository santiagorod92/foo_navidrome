#include "stdafx.h"
#include "BrowserWindow.h"
#include "../../core/NavidromeLibraryPlatform.h"
#include "SubsonicClientWin.h"
#include "../../core/CoreBrowserClient.h"
#include "../../core/NavidromeBrowserEnqueue.h"
#include "../../core/NavidromeAudioMuse.h"
#include "WinUi.h"
#include "BrowserPrompts.h"
#include <uxtheme.h>
#pragma comment(lib, "uxtheme.lib")
#include <SDK/playlist.h>
#include <SDK/metadb.h>
#include <SDK/playable_location.h>
#include <SDK/playback_control.h>
#include <SDK/cfg_var.h>
#include <commctrl.h>
#include <shlobj.h>
#include <algorithm>
#include <cstdio>
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

#include "../../core/NavidromeDebugLog.h"
#include "../../core/NavidromeSettings.h"
static inline void dbgLog(const std::string& msg) { NAVIDROME_LOG("UI", msg); }

using navidrome::win::u8ToWide;
using navidrome::win::wToU8;
using navidrome::win::trw;
using navidrome::tr;
using navidrome::trf;

std::vector<BrowserWindow*> BrowserWindow::s_open;

void BrowserWindow::reloadAllOpen() {
    const auto open = s_open;
    NAVIDROME_LOG("UI", "browser sections changed, reloading " + std::to_string(open.size()) + " open browser(s)");
    for (auto* w : open) {
        if (!w->IsWindow()) continue;
        w->m_search.SetWindowText(L"");
        w->loadArtists();
    }
}

BrowserWindow& BrowserWindow::get() {
    static BrowserWindow inst;
    return inst;
}

void BrowserWindow::show() {
    if (!IsWindow()) {
        Create(nullptr, CWindow::rcDefault, trw("Navidrome Browser").c_str(),
               WS_OVERLAPPEDWINDOW, 0);
        const navidrome::win::UiScale s(*this);
        SetWindowPos(nullptr, 0, 0, s(580), s(660),
                     SWP_NOMOVE | SWP_NOZORDER | SWP_SHOWWINDOW);
        loadArtists();
    } else {
        ShowWindow(SW_SHOW);
        SetForegroundWindow(*this);
        if (navidrome::browserTreeIsStale(m_treeLoadedAtMs, navidrome::browserNowMs())) {
            NAVIDROME_LOG("UI", "show: browse tree is stale, reloading");
            m_search.SetWindowText(L"");
            loadArtists();
        }
    }
}

void BrowserWindow::createEmbedded(HWND parent) {
    m_embedded = true;
    if (IsWindow()) return;
    RECT rc{}; ::GetClientRect(parent, &rc);
    Create(parent, rc, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0);
    loadArtists();
}

LRESULT BrowserWindow::OnCreate(LPCREATESTRUCT) {
    s_open.push_back(this);
    HFONT hFont = navidrome::win::uiFont(*this);
    m_lineH = navidrome::win::lineHeight(*this, hFont);

    m_search.Create(*this, CWindow::rcDefault, nullptr,
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 0, IDC_SEARCH);
    m_search.SetFont(hFont);
    m_search.SetCueBannerText(trw("Search artists, albums, songs…").c_str());
    ::SetWindowSubclass(m_search, &BrowserWindow::SearchSubclassProc, 2,
                        reinterpret_cast<DWORD_PTR>(this));

    m_clearSearchBtn.Create(*this, CWindow::rcDefault, L"\u00D7",
        WS_CHILD | BS_PUSHBUTTON, 0, IDC_CLEAR_SEARCH);
    m_clearSearchBtn.SetFont(hFont);
    m_clearSearchTip.Create(*this, nullptr, nullptr, TTS_ALWAYSTIP | TTS_NOPREFIX);
    const std::wstring clearTip = trw("Clear search (Esc)");
    m_clearSearchTip.AddTool(CToolInfo(TTF_IDISHWND | TTF_SUBCLASS, *this,
        reinterpret_cast<UINT_PTR>(m_clearSearchBtn.m_hWnd), nullptr,
        const_cast<LPWSTR>(clearTip.c_str())));

    m_tree.Create(*this, CWindow::rcDefault, nullptr,
        WS_CHILD | WS_VISIBLE | WS_BORDER | TVS_HASLINES |
        TVS_LINESATROOT | TVS_HASBUTTONS | TVS_SHOWSELALWAYS,
        0, IDC_TREE);
    m_tree.SetFont(hFont);
    ::SetWindowSubclass(m_tree, &BrowserWindow::TreeSubclassProc, 1,
                        reinterpret_cast<DWORD_PTR>(this));

    m_addBtn.Create(*this, CWindow::rcDefault, trw("Add to Playlist").c_str(),
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, IDC_ADD);
    m_addBtn.SetFont(hFont);

    m_playBtn.Create(*this, CWindow::rcDefault, trw("Play Now").c_str(),
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, IDC_PLAY);
    m_playBtn.SetFont(hFont);

    m_refreshBtn.Create(*this, CWindow::rcDefault, trw("Refresh").c_str(),
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, IDC_REFRESH);
    m_refreshBtn.SetFont(hFont);

    m_status.Create(*this, CWindow::rcDefault, nullptr,
        WS_CHILD | WS_VISIBLE | SS_LEFT, 0, IDC_STATUS);
    m_status.SetFont(hFont);

    m_darkMode.AddDialogWithControls(*this);
    refreshThemeColors();

    return 0;
}

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
    s_open.erase(std::remove(s_open.begin(), s_open.end(), this), s_open.end());
    KillTimer(kSearchDebounceTimer);
    ::RemoveWindowSubclass(m_tree, &BrowserWindow::TreeSubclassProc, 1);
    ::RemoveWindowSubclass(m_search, &BrowserWindow::SearchSubclassProc, 2);
    m_selAnchor = nullptr;
    m_nodeMap.clear();
    m_rootNodes.clear();
    m_searchResultNodes.clear();
    if (m_themeBgBrush) { ::DeleteObject(m_themeBgBrush); m_themeBgBrush = nullptr; }
}

LRESULT BrowserWindow::OnSize(UINT, CSize sz) {
    using navidrome::win::fitWidth;
    const navidrome::win::UiScale s(*this);
    const int pad = s(6), btnH = (std::max)(s(26), m_lineH + s(10));
    const int searchH = (std::max)(s(22), m_lineH + s(8));
    int w = sz.cx, h = sz.cy;

    layoutSearchRow();
    m_tree.SetWindowPos(nullptr,
        pad, pad + searchH + pad,
        w - 2*pad, h - searchH - btnH - 4*pad,
        SWP_NOZORDER);

    int btnY = h - pad - btnH;
    const int refreshW = fitWidth(m_refreshBtn, s, 24, 80);
    const int playW = fitWidth(m_playBtn, s, 24, 110), addW = fitWidth(m_addBtn, s, 24, 110);
    m_refreshBtn.SetWindowPos(nullptr, pad, btnY, refreshW, btnH, SWP_NOZORDER);
    m_status.SetWindowPos(nullptr,
        pad + refreshW + pad, btnY + (btnH - m_lineH) / 2,
        (std::max)(0, w - refreshW - playW - addW - 5*pad), m_lineH, SWP_NOZORDER);
    m_playBtn.SetWindowPos(nullptr,
        w - pad - playW, btnY, playW, btnH, SWP_NOZORDER);
    m_addBtn.SetWindowPos(nullptr,
        w - pad - playW - pad - addW, btnY, addW, btnH, SWP_NOZORDER);
    return 0;
}

void BrowserWindow::layoutSearchRow() {
    if (!m_search.IsWindow()) return;
    const navidrome::win::UiScale s(*this);
    const int pad = s(6), searchH = (std::max)(s(22), m_lineH + s(8));
    RECT rc{}; GetClientRect(&rc);
    const int w = rc.right - rc.left;
    const bool showClear = hasSearchText();
    const int clearW = showClear ? searchH : 0;
    const int searchW = (std::max)(0, w - 2*pad - (showClear ? clearW + s(2) : 0));
    m_search.SetWindowPos(nullptr, pad, pad, searchW, searchH, SWP_NOZORDER);
    m_clearSearchBtn.SetWindowPos(nullptr, pad + searchW + s(2), pad, clearW, searchH,
        SWP_NOZORDER | (showClear ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
}

void BrowserWindow::updateClearSearchButton() {
    if (!m_clearSearchBtn.IsWindow()) return;
    if (hasSearchText() != (m_clearSearchBtn.IsWindowVisible() != FALSE)) layoutSearchRow();
}

void BrowserWindow::clearSearch(const char* how) {
    if (!hasSearchText()) return;
    NAVIDROME_LOG("UI", std::string("search cleared (") + how + ")");
    m_search.SetWindowText(L"");
    KillTimer(kSearchDebounceTimer);
    ++m_searchGeneration;
    if (m_isSearching) restoreBrowseTree();
    else setStatus(navidrome::rootLoadSummary(m_rootNodes));
    m_search.SetFocus();
}

LRESULT CALLBACK BrowserWindow::SearchSubclassProc(HWND hWnd, UINT msg, WPARAM wParam,
                                                   LPARAM lParam, UINT_PTR, DWORD_PTR ref) {
    auto* self = reinterpret_cast<BrowserWindow*>(ref);
    switch (msg) {
    case WM_GETDLGCODE: {
        const auto* m = reinterpret_cast<const MSG*>(lParam);
        if (m && m->message == WM_KEYDOWN && m->wParam == VK_ESCAPE && self->hasSearchText())
            return ::DefSubclassProc(hWnd, msg, wParam, lParam) | DLGC_WANTMESSAGE;
        break;
    }
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE && self->hasSearchText()) { self->clearSearch("Esc"); return 0; }
        break;
    case WM_CHAR:
        if (wParam == VK_ESCAPE) return 0;
        break;
    }
    return ::DefSubclassProc(hWnd, msg, wParam, lParam);
}

using navidrome::buildCategoryNodes;

namespace {

navidrome::IBrowserClient& browserClient() {
    static navidrome::CoreBrowserClient inst(navidrome::SubsonicClientWin::get().core(), [] {
        return navidrome::parseHiddenCategories(navidrome::cfg_browser_hidden_categories.get().c_str());
    });
    return inst;
}
}

navidrome::IBrowserClient& navidrome::libraryClient() { return browserClient(); }

void navidrome::showBrowserWindow() {
    fb2k::inMainThread([] { BrowserWindow::get().show(); });
}

bool navidrome::promptForText(const char* title, const char* label, std::string& inOut) {
    std::wstring value;
    if (!navidrome::win::promptText(core_api::get_main_window(), u8ToWide(title).c_str(),
                               u8ToWide(label).c_str(), u8ToWide(inOut), value))
        return false;
    inOut = wToU8(value);
    return true;
}

void BrowserWindow::loadArtists() {
    KillTimer(kSearchDebounceTimer);
    ++m_searchGeneration;
    m_isSearching = false;
    m_searchResultNodes.clear();

    setStatus(tr("Loading artists…"));
    {
        TreeRebuild rebuild(*this);
        clearTree();
    }
    m_rootNodes.clear();
    refreshServerPlaylists();
    refreshRadioStations();

    std::thread([this]() {
        auto* payload = new LoadedPayload{};
        payload->nodes = navidrome::buildRootNodes(browserClient(), payload->error);
        PostMessage(WM_NAVIDROME_LOADED, reinterpret_cast<WPARAM>(payload), 0);
    }).detach();
}

using navidrome::syncBrowserNodesToPlaylists;

std::vector<std::shared_ptr<NavidromeNode>>
BrowserWindow::fetchChildren(const std::shared_ptr<NavidromeNode>& node,
                             std::string& outError) {
    return navidrome::fetchChildren(browserClient(), *node, outError);
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

LRESULT BrowserWindow::OnNavidromeSearch(UINT, WPARAM wParam, LPARAM, BOOL&) {
    auto* payload = reinterpret_cast<LoadedPayload*>(wParam);
    if (payload->generation == m_searchGeneration) populateSearchResults(payload);
    delete payload;
    return 0;
}

LRESULT BrowserWindow::OnNavidromePlaylists(UINT, WPARAM wParam, LPARAM, BOOL&) {
    auto* lists = reinterpret_cast<std::vector<navidrome::Playlist>*>(wParam);
    m_playlistsLoading = false;
    if (lists) { m_serverPlaylists = std::move(*lists); delete lists; }
    return 0;
}

void BrowserWindow::refreshServerPlaylists() {
    if (m_playlistsLoading || !navidrome::SubsonicClientWin::get().isConfigured()) return;
    m_playlistsLoading = true;

    std::thread([this]() {
        std::string err;
        auto lists = navidrome::SubsonicClientWin::get().getPlaylists(err);
        auto* payload = err.empty()
            ? new std::vector<navidrome::Playlist>(std::move(lists))
            : nullptr;
        if (!PostMessage(WM_NAVIDROME_PLAYLISTS, reinterpret_cast<WPARAM>(payload), 0))
            delete payload;
    }).detach();
}

LRESULT BrowserWindow::OnNavidromeRadio(UINT, WPARAM wParam, LPARAM, BOOL&) {
    auto* stations = reinterpret_cast<std::vector<navidrome::RadioStation>*>(wParam);
    m_radioLoading = false;
    if (stations) { m_radioStations = std::move(*stations); delete stations; }
    return 0;
}

void BrowserWindow::refreshRadioStations() {
    if (m_radioLoading || !navidrome::SubsonicClientWin::get().isConfigured()) return;
    m_radioLoading = true;

    std::thread([this]() {
        std::string err;
        auto stations = navidrome::SubsonicClientWin::get().getRadioStations(err);
        auto* payload = err.empty()
            ? new std::vector<navidrome::RadioStation>(std::move(stations))
            : nullptr;
        if (!PostMessage(WM_NAVIDROME_RADIO, reinterpret_cast<WPARAM>(payload), 0))
            delete payload;
    }).detach();
}

std::string BrowserWindow::radioStationURL(const std::string& stationId) {
    for (auto& s : m_radioStations)
        if (s.id == stationId) return s.streamUrl;
    return "";
}

void BrowserWindow::populateRoot(LoadedPayload* payload) {
    if (!payload->error.empty()) {
        m_reloadNotice.clear();
        setStatus(trf("Error: {0}", { payload->error })); return;
    }
    m_rootNodes = payload->nodes;
    m_treeLoadedAtMs = navidrome::browserNowMs();
    {
        NAVIDROME_TIMER("UI", "populateRoot " + std::to_string(m_rootNodes.size()) + " rows");
        TreeRebuild rebuild(*this);
        for (auto& n : m_rootNodes) insertNode(TVI_ROOT, n);
    }
    if (!m_reloadNotice.empty()) { setStatus(m_reloadNotice); m_reloadNotice.clear(); }
    else setStatus(navidrome::rootLoadSummary(m_rootNodes));
}

void BrowserWindow::populateSearchResults(LoadedPayload* payload) {
    if (!payload->error.empty()) {
        setStatus(trf("Search error: {0}", { payload->error })); return;
    }
    m_isSearching       = true;
    m_searchResultNodes = payload->nodes;
    {
        NAVIDROME_TIMER("UI", "populateSearchResults " + std::to_string(m_searchResultNodes.size()) + " rows");
        TreeRebuild rebuild(*this);
        clearTree();
        for (auto& group : m_searchResultNodes) {
            HTREEITEM hGroup = insertNode(TVI_ROOT, group);
            for (auto& n : group->children) insertNode(hGroup, n);
            m_tree.Expand(hGroup, TVE_EXPAND);
        }
    }
    setStatus(navidrome::searchSummary(m_searchResultNodes));
}

void BrowserWindow::restoreBrowseTree() {
    m_isSearching = false;
    m_searchResultNodes.clear();
    {
        NAVIDROME_TIMER("UI", "restoreBrowseTree " + std::to_string(m_rootNodes.size()) + " rows");
        TreeRebuild rebuild(*this);
        clearTree();
        for (auto& n : m_rootNodes) {
            n->children.clear();
            n->childrenLoaded = false;
            SetNodeItem(n, nullptr);
            insertNode(TVI_ROOT, n);
        }
    }
    setStatus(navidrome::rootLoadSummary(m_rootNodes));
}

void BrowserWindow::populateChildren(LoadedPayload* payload) {
    auto parent = payload->parent;
    if (!parent) return;

    HTREEITEM hChild = m_tree.GetChildItem(NodeItem(parent));
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
        errNode->displayName = trf("Error: {0}", { payload->error });
        parent->children     = { errNode };
    }

    {
        TreeRebuild rebuild(*this);
        for (auto& child : parent->children)
            insertNode(NodeItem(parent), child);
    }

    if (parent->children.empty()) {
        TVITEM it   = {};
        it.mask     = TVIF_CHILDREN;
        it.hItem    = NodeItem(parent);
        it.cChildren = 0;
        m_tree.SetItem(&it);
    }
}

BrowserWindow::TreeRebuild::TreeRebuild(BrowserWindow& w) : w(w) {
    w.m_treeRebuilding = true;
    w.m_tree.SetRedraw(FALSE);
}

BrowserWindow::TreeRebuild::~TreeRebuild() {
    w.m_tree.SetRedraw(TRUE);
    w.m_tree.Invalidate();
    w.m_treeRebuilding = false;
}

void BrowserWindow::clearTree() {
    m_tree.SelectItem(nullptr);
    m_tree.DeleteAllItems();
    m_nodeMap.clear();
    m_selAnchor = nullptr;
}

std::string BrowserWindow::labelFor(const std::shared_ptr<NavidromeNode>& node) const {
    std::string label = navidrome::singleColumnLabel(*node);
    if (m_isSearching && node->type == NavidromeNode::Song && !node->subtitle.empty())
        label += " \u2014 " + node->subtitle;
    return label;
}

void BrowserWindow::refreshLabel(const std::shared_ptr<NavidromeNode>& node) {
    if (!node || !NodeItem(node)) return;
    m_tree.SetItemText(NodeItem(node), u8ToWide(labelFor(node)).c_str());
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
    tvi.item.cChildren    = navidrome::isLeaf(*node) ? 0 : 1;

    HTREEITEM hItem = m_tree.InsertItem(&tvi);
    SetNodeItem(node, hItem);
    m_nodeMap[hItem] = node;
    return hItem;
}

std::shared_ptr<NavidromeNode> BrowserWindow::nodeForItem(HTREEITEM hItem) {
    auto it = m_nodeMap.find(hItem);
    return (it != m_nodeMap.end()) ? it->second : nullptr;
}

LRESULT BrowserWindow::OnTreeExpanding(LPNMHDR pnmh) {
    auto* pnm = reinterpret_cast<LPNMTREEVIEW>(pnmh);
    if (pnm->action != TVE_EXPAND) return 0;

    auto node = nodeForItem(pnm->itemNew.hItem);
    if (!node || node->childrenLoaded || node->isLoading) return 0;
    node->isLoading = true;

    auto loadNode = std::make_shared<NavidromeNode>();
    loadNode->type        = NavidromeNode::Loading;
    loadNode->displayName = tr("Loading…");
    insertNode(NodeItem(node), loadNode);

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
    if (node->type == NavidromeNode::Song || node->type == NavidromeNode::Radio)
        enqueueNodes({ node }, true);
    else if (navidrome::isAllSongsNode(*node))
        queueNodes({ node }, true, false);
    else if (m_tree.GetItemState(hSel, TVIS_EXPANDED) & TVIS_EXPANDED)
        m_tree.Expand(hSel, TVE_COLLAPSE);
    else
        m_tree.Expand(hSel, TVE_EXPAND);
    return 0;
}

std::vector<std::shared_ptr<NavidromeNode>> BrowserWindow::selectedNodes() {
    std::vector<std::shared_ptr<NavidromeNode>> selected;
    for (HTREEITEM hItem : visibleItems()) {
        if (!isSelected(hItem)) continue;
        auto n = nodeForItem(hItem);
        if (n && n->type != NavidromeNode::Loading && n->type != NavidromeNode::Error)
            selected.push_back(n);
    }
    return selected;
}

std::vector<HTREEITEM> BrowserWindow::visibleItems() {
    std::vector<HTREEITEM> out;
    for (HTREEITEM h = m_tree.GetRootItem(); h; h = m_tree.GetNextVisibleItem(h))
        out.push_back(h);
    return out;
}

bool BrowserWindow::isSelected(HTREEITEM h) {
    return (m_tree.GetItemState(h, TVIS_SELECTED) & TVIS_SELECTED) != 0;
}

void BrowserWindow::clearSelectionExcept(HTREEITEM keep) {
    for (HTREEITEM h : visibleItems())
        if (h != keep && isSelected(h)) m_tree.SetItemState(h, 0, TVIS_SELECTED);
    if (keep) m_tree.SetItemState(keep, TVIS_SELECTED, TVIS_SELECTED);
}

void BrowserWindow::selectRange(HTREEITEM from, HTREEITEM to) {
    auto items = visibleItems();
    auto a = std::find(items.begin(), items.end(), from);
    auto b = std::find(items.begin(), items.end(), to);
    if (a == items.end()) a = b;
    if (b == items.end()) return;
    if (a > b) std::swap(a, b);
    for (auto it = items.begin(); it != items.end(); ++it) {
        bool in = it >= a && it <= b;
        if (in != isSelected(*it))
            m_tree.SetItemState(*it, in ? TVIS_SELECTED : 0, TVIS_SELECTED);
    }
}

bool BrowserWindow::onTreeLButtonDown(LPARAM lParam) {
    TVHITTESTINFO ht = {};
    ht.pt = { static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)) };
    HTREEITEM hit = m_tree.HitTest(&ht);
    if (!hit || !(ht.flags & TVHT_ONITEM)) return false;

    const bool ctrl  = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool shift = (::GetKeyState(VK_SHIFT)   & 0x8000) != 0;
    if (!ctrl && !shift) {
        clearSelectionExcept(hit);
        m_selAnchor = hit;
        return false;
    }

    m_tree.SetFocus();
    if (shift && m_selAnchor && m_nodeMap.count(m_selAnchor)) {
        selectRange(m_selAnchor, hit);
    } else {
        m_tree.SetItemState(hit, isSelected(hit) ? 0 : TVIS_SELECTED, TVIS_SELECTED);
        m_selAnchor = hit;
    }
    return true;
}

LRESULT CALLBACK BrowserWindow::TreeSubclassProc(HWND hWnd, UINT msg, WPARAM wParam,
                                                 LPARAM lParam, UINT_PTR, DWORD_PTR ref) {
    auto* self = reinterpret_cast<BrowserWindow*>(ref);
    if (msg == WM_LBUTTONDOWN && self->onTreeLButtonDown(lParam)) return 0;
    return ::DefSubclassProc(hWnd, msg, wParam, lParam);
}

LRESULT BrowserWindow::OnTreeSelChanged(LPNMHDR pnmh) {
    if (m_treeRebuilding) return 0;
    auto* pnm = reinterpret_cast<LPNMTREEVIEW>(pnmh);
    HTREEITEM now = pnm->itemNew.hItem;
    if (!now) return 0;
    const bool shift = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
    if (pnm->action == TVC_BYKEYBOARD && shift && m_selAnchor && m_nodeMap.count(m_selAnchor)) {
        selectRange(m_selAnchor, now);
    } else {
        clearSelectionExcept(now);
        m_selAnchor = now;
    }
    return 0;
}

void BrowserWindow::queueSelected(bool play, bool closeAfter) {
    queueNodes(selectedNodes(), play, closeAfter);
}

void BrowserWindow::queueNodes(std::vector<std::shared_ptr<NavidromeNode>> selected,
                               bool play, bool closeAfter) {
    if (selected.empty()) { setStatus(tr("Select at least one item")); return; }

    NAVIDROME_LOG("UI", "queueNodes: " + std::to_string(selected.size()) +
                  " selected node(s), play=" + (play ? "1" : "0"));
    setStatus(tr("Loading tracks…"));
    std::thread([this, selected, play, closeAfter]() {
        std::string err;
        auto songs = navidrome::collectSelectionSongs(browserClient(), selected, &err);
        if (!err.empty()) NAVIDROME_WARN("UI", "queueNodes: " + err);
        fb2k::inMainThread([this, songs, err, play, closeAfter]() mutable {
            if (!IsWindow()) return;
            const bool stale = navidrome::browserTreeIsStale(m_treeLoadedAtMs,
                                                             navidrome::browserNowMs());
            const bool reload =
                navidrome::shouldReloadAfterEmptyCollect(!songs.empty(), !err.empty(), stale);
            const std::string problem =
                navidrome::queueProblemMessage(!songs.empty(), err, reload);
            if (reload) {
                NAVIDROME_WARN("UI", "queueNodes: nothing to queue, reloading the browse tree");
                m_reloadNotice = tr("Couldn't load tracks — list reloaded, select again");
                m_search.SetWindowText(L"");
                loadArtists();
            }
            if (!problem.empty()) navidrome::showBrowserQueueError(problem);
            if (reload) return;
            if (songs.empty()) {
                NAVIDROME_WARN("UI", "queueNodes: the selection resolved to no tracks");
                setStatus(tr("No tracks found — try Refresh"));
                return;
            }
            enqueueNodes(std::move(songs), play);
            if (closeAfter && !m_embedded && IsWindow()) ShowWindow(SW_HIDE);
        });
    }).detach();
}

void BrowserWindow::OnAdd(UINT, int, HWND)  { dbgLog("OnAdd fired"); queueSelected(false, false); }
void BrowserWindow::OnPlay(UINT, int, HWND) { dbgLog("OnPlay fired"); queueSelected(true,  false); }

void BrowserWindow::OnPlaySimilar(UINT, int, HWND) {
    dbgLog("OnPlaySimilar (Instant Mix) fired");
    auto selected = selectedNodes();
    auto node = selected.empty() ? nullptr : selected.front();
    if (!node || !navidrome::isSimilarEligible(*node)) {
        setStatus(tr("Instant Mix needs an artist, album, or song"));
        return;
    }
    navidrome::startInstantMix(node);
}

void BrowserWindow::OnAlchemy(UINT, int, HWND) {
    dbgLog("OnAlchemy fired");
    std::string label;
    auto seeds = navidrome::audiomuse::seedsFromNodes(selectedNodes(), label);
    if (seeds.empty()) { setStatus(tr("Song Alchemy needs songs or artists")); return; }
    navidrome::audioMuseAlchemy(std::move(seeds), std::move(label));
}

void BrowserWindow::OnArtistInfo(UINT, int, HWND) {
    dbgLog("OnArtistInfo fired");
    auto selected = selectedNodes();
    auto node = selected.empty() ? nullptr : selected.front();
    if (!node || node->type != navidrome::BrowserNode::Artist) {
        setStatus(tr("Artist Info needs an artist"));
        return;
    }

    setStatus(tr("Fetching artist info…"));
    std::string artistId   = node->id;
    std::wstring artistName = u8ToWide(node->displayName);
    std::thread([this, artistId, artistName]() {
        std::string err;
        auto info = navidrome::SubsonicClientWin::get().getArtistInfo(artistId, err);
        std::wstring text = u8ToWide(navidrome::formatArtistBiography(info));
        fb2k::inMainThread([this, artistName, text, err]() mutable {
            if (!IsWindow()) return;
            if (!err.empty()) { setStatus(trf("Error: {0}", { err })); return; }
            setStatus("");
            MessageBoxW(text.c_str(), artistName.c_str(), MB_OK | MB_ICONINFORMATION);
        });
    }).detach();
}

void BrowserWindow::OnAlbumInfo(UINT, int, HWND) {
    dbgLog("OnAlbumInfo fired");
    auto selected = selectedNodes();
    auto target = selected.empty() ? navidrome::AlbumInfoTarget{}
                                   : navidrome::albumInfoTarget(*selected.front());
    if (target.albumId.empty()) {
        setStatus(tr("Album Info needs an album or a song"));
        return;
    }

    setStatus(tr("Fetching album info…"));
    std::thread([this, target]() {
        std::string err;
        auto info = browserClient().getAlbumInfo(target.albumId, err);
        if (!err.empty()) NAVIDROME_WARN("UI", "album info " + target.albumId + ": " + err);
        else dbgLog("album info " + target.albumId + ": notes=" +
                    (info.notes.empty() ? "none" : std::to_string(info.notes.size()) + " chars"));
        std::wstring text  = u8ToWide(navidrome::formatAlbumNotes(info));
        std::wstring title = u8ToWide(target.title);
        fb2k::inMainThread([this, title, text, err]() mutable {
            if (!IsWindow()) return;
            if (!err.empty()) { setStatus(trf("Error: {0}", { err })); return; }
            setStatus("");
            MessageBoxW(text.c_str(), title.c_str(), MB_OK | MB_ICONINFORMATION);
        });
    }).detach();
}

void BrowserWindow::OnRandomMix(UINT, int, HWND) {
    dbgLog("OnRandomMix fired");
    setStatus(tr("Fetching random mix…"));
    std::thread([this]() {
        std::string err;
        auto nodes = navidrome::fetchRandomMix(browserClient(), 100, err);
        fb2k::inMainThread([this, nodes, err]() mutable {
            if (!IsWindow()) return;
            if (!err.empty()) { setStatus(trf("Error: {0}", { err })); return; }
            if (nodes.empty()) { setStatus(tr("No tracks found")); return; }
            enqueueNodes(std::move(nodes), true);
        });
    }).detach();
}

LRESULT BrowserWindow::OnTreeReturn(LPNMHDR) {
    queueSelected(true, true);
    return 0;
}

void BrowserWindow::OnContextMenu(CWindow wnd, CPoint point) {
    dbgLog("OnContextMenu: wnd=" + std::to_string(reinterpret_cast<uintptr_t>(wnd.m_hWnd)) +
           " tree=" + std::to_string(reinterpret_cast<uintptr_t>(m_tree.m_hWnd)) +
           " point=" + std::to_string(point.x) + "," + std::to_string(point.y));
    if (wnd.m_hWnd != m_tree.m_hWnd) {
        dbgLog("OnContextMenu: wnd mismatch, passing through");
        SetMsgHandled(FALSE); return;
    }
    if (m_passContextMenu && m_passContextMenu()) {
        GetParent().SendMessage(WM_CONTEXTMENU, reinterpret_cast<WPARAM>(wnd.m_hWnd),
                                MAKELPARAM(point.x, point.y));
        return;
    }

    if (point.x == -1 && point.y == -1) {
        HTREEITEM sel = m_tree.GetSelectedItem();
        CRect rc;
        if (sel && m_tree.GetItemRect(sel, &rc, TRUE)) point = rc.CenterPoint();
        else { m_tree.GetClientRect(&rc); point = rc.TopLeft(); }
        m_tree.ClientToScreen(&point);
    } else {
        CPoint client(point);
        m_tree.ScreenToClient(&client);
        UINT flags = 0;
        HTREEITEM hit = m_tree.HitTest(client, &flags);
        if (hit && !isSelected(hit)) m_tree.SelectItem(hit);
    }

    auto selForMenu = selectedNodes();
    dbgLog("OnContextMenu: selectedNodes count=" + std::to_string(selForMenu.size()));
    if (selForMenu.empty()) { dbgLog("OnContextMenu: empty selection, aborting"); return; }

    CMenu menu;
    menu.CreatePopupMenu();
    menu.AppendMenu(MF_STRING, IDC_PLAY, trw("Play Now").c_str());
    menu.AppendMenu(MF_STRING, IDC_ADD,  trw("Add to Playlist").c_str());
    menu.AppendMenu(MF_STRING, IDC_PLAY_SIMILAR, trw("Instant Mix").c_str());
    if (navidrome::audioMuseSettings().configured())
        menu.AppendMenu(MF_STRING, IDC_ALCHEMY, trw("Song Alchemy (AudioMuse-AI)").c_str());
    menu.AppendMenu(MF_STRING, IDC_ARTIST_INFO,  trw("Artist Info").c_str());
    menu.AppendMenu(MF_STRING, IDC_ALBUM_INFO,   trw("Album Info").c_str());

    menu.AppendMenu(MF_SEPARATOR);
    menu.AppendMenu(MF_STRING, IDC_STAR,   trw("Star").c_str());
    menu.AppendMenu(MF_STRING, IDC_UNSTAR, trw("Unstar").c_str());
    menu.AppendMenu(MF_STRING, IDC_REMOVE_BOOKMARK, trw("Remove Bookmark").c_str());

    CMenu rating;
    rating.CreatePopupMenu();
    rating.AppendMenu(MF_STRING, IDC_RATE_0, trw("None").c_str());
    static const wchar_t* kStars[] = { L"★", L"★★", L"★★★", L"★★★★", L"★★★★★" };
    for (int i = 0; i < 5; ++i)
        rating.AppendMenu(MF_STRING, IDC_RATE_0 + 1 + i, kStars[i]);
    menu.AppendMenu(MF_POPUP, reinterpret_cast<UINT_PTR>(rating.m_hMenu), trw("Rating").c_str());
    rating.Detach();

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
            m_playlistsLoading ? trw("Loading…").c_str() : trw("No playlists on server").c_str());
    }
    playlists.AppendMenu(MF_SEPARATOR);
    playlists.AppendMenu(MF_STRING, IDC_NEW_PLAYLIST, trw("New Playlist…").c_str());
    menu.AppendMenu(MF_POPUP, reinterpret_cast<UINT_PTR>(playlists.m_hMenu),
                    trw("Add to Navidrome Playlist").c_str());
    playlists.Detach();

    menu.AppendMenu(MF_STRING, IDC_REMOVE_FROM_PL,  trw("Remove from Playlist").c_str());
    menu.AppendMenu(MF_STRING, IDC_RENAME_PLAYLIST, trw("Rename Playlist…").c_str());
    menu.AppendMenu(MF_STRING, IDC_DELETE_PLAYLIST, trw("Delete Playlist…").c_str());

    menu.AppendMenu(MF_SEPARATOR);
    menu.AppendMenu(MF_STRING, IDC_NEW_RADIO,    trw("New Radio Station…").c_str());
    menu.AppendMenu(MF_STRING, IDC_EDIT_RADIO,   trw("Edit Radio Station…").c_str());
    menu.AppendMenu(MF_STRING, IDC_DELETE_RADIO, trw("Delete Radio Station…").c_str());

    menu.AppendMenu(MF_SEPARATOR);
    menu.AppendMenu(MF_STRING, IDC_SUBSCRIBE_PODCAST,   trw("Subscribe to Podcast…").c_str());
    menu.AppendMenu(MF_STRING, IDC_UNSUBSCRIBE_PODCAST, trw("Unsubscribe from Podcast…").c_str());

    menu.AppendMenu(MF_SEPARATOR);
    menu.AppendMenu(MF_STRING, IDC_SEND_PLAYLIST,
                    trw("Send Active Playlist to Navidrome").c_str());
    menu.AppendMenu(MF_STRING, IDC_RANDOM_MIX, trw("Random Mix").c_str());
    menu.AppendMenu(MF_STRING, IDC_DOWNLOAD, trw("Download Original Files…").c_str());
    menu.AppendMenu(MF_STRING, IDC_MAKE_OFFLINE, trw("Make Available Offline").c_str());
    menu.AppendMenu(MF_STRING, IDC_REMOVE_OFFLINE, trw("Remove Offline Copy").c_str());

    dbgLog("OnContextMenu: showing TrackPopupMenu");
    menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y, *this);
    dbgLog("OnContextMenu: TrackPopupMenu returned");

    refreshServerPlaylists();
    refreshRadioStations();
}

void BrowserWindow::setStatus(const std::string& msg) {
    m_status.SetWindowText(u8ToWide(msg).c_str());
}
