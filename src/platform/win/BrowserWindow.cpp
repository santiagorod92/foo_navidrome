#include "stdafx.h"
#include "BrowserWindow.h"
#include "../../core/NavidromeLibraryPlatform.h"
#include "SubsonicClientWin.h"
#include "../../core/NavidromeBrowserEnqueue.h"
#include "../../core/NavidromeAudioMuse.h"
#include "WinUi.h"
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

// Config vars defined in NavidromePluginWin.cpp — the Libraries prefs sub-page
// (below) reads and writes the multi-library filter toggle + id selection.
namespace navidrome {
    extern cfg_var_modern::cfg_bool cfg_library_filter;
    extern cfg_string cfg_library_ids;
}

// Debug-only tracing — see Windows/NavidromeDebugLog.h. The shared tracer adds a
// timestamp + level + tag and is what `make win-logs` pretty-prints live.
// This shim keeps the existing bare-message call sites; they log under "UI".
#include "../../core/NavidromeDebugLog.h"
static inline void dbgLog(const std::string& msg) { NAVIDROME_LOG("UI", msg); }

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

// Sizes a prompt popup to its laid-out client area and centers it on the owner
// (or leaves it where Windows put it when there isn't one).
void placePrompt(CWindow& w, HWND owner, SIZE client) {
    RECT rc{ 0, 0, client.cx, client.cy };
    ::AdjustWindowRectEx(&rc, static_cast<DWORD>(w.GetWindowLongPtr(GWL_STYLE)), FALSE,
                         static_cast<DWORD>(w.GetWindowLongPtr(GWL_EXSTYLE)));
    const int ww = rc.right - rc.left, wh = rc.bottom - rc.top;
    RECT rcOwner{};
    if (owner && ::GetWindowRect(owner, &rcOwner)) {
        int x = rcOwner.left + ((rcOwner.right - rcOwner.left) - ww) / 2;
        int y = rcOwner.top + ((rcOwner.bottom - rcOwner.top) - wh) / 2;
        w.SetWindowPos(nullptr, x, y, ww, wh, SWP_NOZORDER);
    } else {
        w.SetWindowPos(nullptr, 0, 0, ww, wh, SWP_NOMOVE | SWP_NOZORDER);
    }
}

// Creates the OK (default) + Cancel pair right-aligned to `right` at row `y`;
// returns the client size that fits everything above plus the buttons.
SIZE layoutOkCancel(HWND parent, HFONT f, const navidrome::win::UiScale& s,
                    int right, int y, int btnH) {
    using namespace navidrome::win;
    auto mk = [&](const wchar_t* text, DWORD style, int id) {
        HWND b = CreateWindowW(L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | style,
            0, 0, 0, 0, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
        setFont(b, f);
        return b;
    };
    HWND ok = mk(L"OK", BS_DEFPUSHBUTTON, IDOK);
    HWND cancel = mk(L"Cancel", BS_PUSHBUTTON, IDCANCEL);
    const int okW = fitWidth(ok, s, 24, 76), cancelW = fitWidth(cancel, s, 24, 76);
    ::SetWindowPos(cancel, nullptr, right - cancelW, y, cancelW, btnH, SWP_NOZORDER);
    ::SetWindowPos(ok, nullptr, right - cancelW - s(8) - okW, y, okW, btnH, SWP_NOZORDER);
    return SIZE{ right + s(12), y + btnH + s(12) };
}

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

        placePrompt(w, owner, w.m_clientSize);

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
        MSG_WM_ERASEBKGND(OnEraseBkgnd)
        MSG_WM_CREATE(OnCreate)
        MSG_WM_CLOSE(OnClose)
        COMMAND_ID_HANDLER_EX(IDOK,     OnOk)
        COMMAND_ID_HANDLER_EX(IDCANCEL, OnCancel)
    END_MSG_MAP()

    BOOL OnEraseBkgnd(CDCHandle dc) { navidrome::win::eraseLikeDialog(*this, dc); return TRUE; }

private:
    enum { IDC_PROMPT_LABEL = 4001, IDC_PROMPT_EDIT = 4002 };

    LRESULT OnCreate(LPCREATESTRUCT) {
        using namespace navidrome::win;
        const UiScale s(*this);
        const HFONT f = uiFont(*this);
        const int pad = s(12), fieldW = s(330), lineH = lineHeight(*this, f);
        const int rowH = (std::max)(s(22), lineH + s(8));

        HWND label = CreateWindowW(L"STATIC", m_label.c_str(), WS_CHILD | WS_VISIBLE,
            pad, pad, fieldW, lineH, *this,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PROMPT_LABEL)), nullptr, nullptr);
        setFont(label, f);

        int y = pad + lineH + s(4);
        m_edit.Create(*this, CWindow::rcDefault, nullptr,
            WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL,
            0, IDC_PROMPT_EDIT);
        m_edit.SetWindowPos(nullptr, pad, y, fieldW, rowH, SWP_NOZORDER);
        m_edit.SetFont(f);
        m_edit.SetWindowText(m_value.c_str());
        m_edit.SetSel(0, -1);
        y += rowH + s(12);

        // BS_DEFPUSHBUTTON is what makes IsDialogMessage translate Enter to IDOK.
        m_clientSize = layoutOkCancel(*this, f, s, pad + fieldW, y, rowH + s(4));
        m_darkMode.AddDialogWithControls(*this);
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
    SIZE         m_clientSize{};
    bool         m_accepted = false;
    bool         m_done     = false;
    fb2k::CCoreDarkModeHooks m_darkMode;
};

// ---------------------------------------------------------------------------
// Modal 3-field prompt for New/Edit Radio Station (name / stream URL / home
// page URL). Same message-pump-driven popup technique as TextPromptWindow,
// just with three stacked labeled edits instead of one.
// ---------------------------------------------------------------------------
class RadioStationPromptWindow : public CWindowImpl<RadioStationPromptWindow> {
public:
    DECLARE_WND_CLASS(L"foo_navidrome_RadioPromptWnd")

    static bool run(HWND owner, const wchar_t* title,
                    const std::wstring& initialName,
                    const std::wstring& initialStreamURL,
                    const std::wstring& initialHomePageURL,
                    std::wstring& outName, std::wstring& outStreamURL,
                    std::wstring& outHomePageURL) {
        RadioStationPromptWindow w;
        w.m_name        = initialName;
        w.m_streamURL   = initialStreamURL;
        w.m_homePageURL = initialHomePageURL;

        w.Create(owner, CWindow::rcDefault, title,
                 WS_POPUP | WS_CAPTION | WS_SYSMENU, WS_EX_DLGMODALFRAME);
        if (!w.IsWindow()) return false;

        placePrompt(w, owner, w.m_clientSize);

        if (owner) ::EnableWindow(owner, FALSE);
        w.ShowWindow(SW_SHOW);
        w.m_nameEdit.SetFocus();

        MSG msg;
        while (!w.m_done) {
            BOOL got = ::GetMessageW(&msg, nullptr, 0, 0);
            if (got == 0) {
                ::PostQuitMessage(static_cast<int>(msg.wParam));
                break;
            }
            if (got == -1) break;
            if (!::IsDialogMessageW(w.m_hWnd, &msg)) {
                ::TranslateMessage(&msg);
                ::DispatchMessageW(&msg);
            }
        }
        if (owner) { ::EnableWindow(owner, TRUE); ::SetForegroundWindow(owner); }
        if (w.IsWindow()) w.DestroyWindow();

        outName        = w.m_name;
        outStreamURL   = w.m_streamURL;
        outHomePageURL = w.m_homePageURL;
        return w.m_accepted;
    }

    BEGIN_MSG_MAP(RadioStationPromptWindow)
        MSG_WM_ERASEBKGND(OnEraseBkgnd)
        MSG_WM_CREATE(OnCreate)
        MSG_WM_CLOSE(OnClose)
        COMMAND_ID_HANDLER_EX(IDOK,     OnOk)
        COMMAND_ID_HANDLER_EX(IDCANCEL, OnCancel)
    END_MSG_MAP()

    BOOL OnEraseBkgnd(CDCHandle dc) { navidrome::win::eraseLikeDialog(*this, dc); return TRUE; }

private:
    enum {
        IDC_NAME_LABEL = 4011, IDC_NAME_EDIT = 4012,
        IDC_URL_LABEL  = 4013, IDC_URL_EDIT  = 4014,
        IDC_HOME_LABEL = 4015, IDC_HOME_EDIT = 4016,
    };

    LRESULT OnCreate(LPCREATESTRUCT) {
        using namespace navidrome::win;
        const UiScale s(*this);
        const HFONT f = uiFont(*this);
        const int pad = s(12), fieldW = s(330), lineH = lineHeight(*this, f);
        const int rowH = (std::max)(s(22), lineH + s(8));

        int y = pad;
        auto field = [&](int labelId, const wchar_t* text, CEdit& edit, int editId,
                         const std::wstring& value) {
            setFont(CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE,
                pad, y, fieldW, lineH, *this,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(labelId)), nullptr, nullptr), f);
            y += lineH + s(4);
            edit.Create(*this, CWindow::rcDefault, nullptr,
                WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 0, editId);
            edit.SetWindowPos(nullptr, pad, y, fieldW, rowH, SWP_NOZORDER);
            edit.SetFont(f);
            edit.SetWindowText(value.c_str());
            y += rowH + s(10);
        };
        field(IDC_NAME_LABEL, L"Name:", m_nameEdit, IDC_NAME_EDIT, m_name);
        m_nameEdit.SetSel(0, -1);
        field(IDC_URL_LABEL, L"Stream URL:", m_urlEdit, IDC_URL_EDIT, m_streamURL);
        field(IDC_HOME_LABEL, L"Home page URL (optional):", m_homeEdit, IDC_HOME_EDIT, m_homePageURL);

        m_clientSize = layoutOkCancel(*this, f, s, pad + fieldW, y + s(2), rowH + s(4));
        m_darkMode.AddDialogWithControls(*this);
        return 0;
    }

    static std::wstring textOf(CEdit& edit) {
        int len = edit.GetWindowTextLength();
        std::wstring buf(static_cast<std::size_t>(len) + 1, L'\0');
        edit.GetWindowText(&buf[0], len + 1);
        buf.resize(static_cast<std::size_t>(len));
        return buf;
    }

    void OnOk(UINT, int, HWND) {
        m_name        = textOf(m_nameEdit);
        m_streamURL   = textOf(m_urlEdit);
        m_homePageURL = textOf(m_homeEdit);
        m_accepted    = true;
        m_done        = true;
    }

    void OnCancel(UINT, int, HWND) { m_done = true; }
    void OnClose()                 { m_done = true; }

    CEdit        m_nameEdit, m_urlEdit, m_homeEdit;
    std::wstring m_name, m_streamURL, m_homePageURL;
    SIZE         m_clientSize{};
    bool         m_accepted = false;
    bool         m_done     = false;
    fb2k::CCoreDarkModeHooks m_darkMode;
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

// Dark Mode for a prefs page holding a SysListView32. coreDarkMode's hook for
// a list view left it unpainted (blank, under Wine at least — issue #18), so
// the page and every other child go through the hooks and a list view is
// themed by hand, only when foobar is dark.
void addDarkModeHooksKeepingLists(fb2k::CCoreDarkModeHooks& dark, HWND page) {
    dark.AddDialog(page);
    for (HWND c = ::GetWindow(page, GW_CHILD); c; c = ::GetWindow(c, GW_HWNDNEXT)) {
        wchar_t cls[64] = {};
        ::GetClassNameW(c, cls, 64);
        if (::lstrcmpiW(cls, WC_LISTVIEWW) != 0) { dark.AddCtrlAuto(c); continue; }
        if (!dark) continue;
        const COLORREF bg = RGB(0x20, 0x20, 0x20), text = RGB(0xDE, 0xDE, 0xDE);
        ListView_SetBkColor(c, bg);
        ListView_SetTextBkColor(c, bg);
        ListView_SetTextColor(c, text);
        ::SetWindowTheme(c, L"DarkMode_Explorer", nullptr);       // scrollbars
        if (HWND header = ListView_GetHeader(c))
            ::SetWindowTheme(header, L"DarkMode_ItemsView", nullptr);
    }
}

// ---------------------------------------------------------------------------
// Preferences > Media Library > Navidrome > Radio Stations — dedicated
// sub-page nested under the main Navidrome credentials page (guid_prefs_page)
// so it shows as a child entry, not a sibling under Tools. Lists the
// server's configured stations with Add/Edit/Delete, entirely independent of
// any open BrowserWindow (own fetch, own cached station list). Reuses
// RadioStationPromptWindow as-is for the Add/Edit modal.
// ---------------------------------------------------------------------------
class NavidromeRadioPrefsInstance : public CWindowImpl<NavidromeRadioPrefsInstance>,
                                    public preferences_page_instance {
public:
    DECLARE_WND_CLASS(L"foo_navidrome_RadioPrefsWnd")

    explicit NavidromeRadioPrefsInstance(preferences_page_callback::ptr cb) : m_cb(cb) {}

    // Read-only management view — nothing here is "applied", every action is
    // a live server request, so this page never reports itself as changed.
    HWND     get_wnd() override { return m_hWnd; }
    t_uint32 get_state() override { return preferences_state::dark_mode_supported; }
    void     apply() override {}
    void     reset() override {}

    enum { WM_RADIO_LOADED = WM_USER + 200 };

    BEGIN_MSG_MAP(NavidromeRadioPrefsInstance)
        MSG_WM_ERASEBKGND(OnEraseBkgnd)
        MSG_WM_CREATE(OnCreate)
        MSG_WM_SIZE(OnSize)
        MESSAGE_HANDLER_EX(WM_RADIO_LOADED, OnRadioLoaded)
        COMMAND_ID_HANDLER_EX(IDC_NEW,    OnNew)
        COMMAND_ID_HANDLER_EX(IDC_EDIT,   OnEdit)
        COMMAND_ID_HANDLER_EX(IDC_DELETE, OnDelete)
    END_MSG_MAP()

    BOOL OnEraseBkgnd(CDCHandle dc) { navidrome::win::eraseLikeDialog(*this, dc); return TRUE; }

private:
    enum { IDC_LIST = 5001, IDC_NEW = 5002, IDC_EDIT = 5003, IDC_DELETE = 5004, IDC_STATUS = 5005 };

    LRESULT OnCreate(LPCREATESTRUCT) {
        m_list.Create(*this, CWindow::rcDefault, nullptr,
            WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL | WS_TABSTOP,
            WS_EX_CLIENTEDGE, IDC_LIST);
        m_list.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT);
        const navidrome::win::UiScale s(*this);
        m_list.InsertColumn(0, L"Name", LVCFMT_LEFT, s(150));
        m_list.InsertColumn(1, L"Stream URL", LVCFMT_LEFT, s(260));
        m_list.InsertColumn(2, L"Home Page", LVCFMT_LEFT, s(180));

        HFONT f = navidrome::win::uiFont(*this);
        m_list.SetFont(f);
        m_lineH = navidrome::win::lineHeight(*this, f);
        auto mkButton = [&](int id, const wchar_t* text) {
            HWND h = CreateWindowW(L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                0, 0, 0, 0, *this, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
            navidrome::win::setFont(h, f);
            return h;
        };
        m_newBtn    = mkButton(IDC_NEW, L"New\u2026");
        m_editBtn   = mkButton(IDC_EDIT, L"Edit\u2026");
        m_deleteBtn = mkButton(IDC_DELETE, L"Delete\u2026");

        m_status = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_ENDELLIPSIS,
            0, 0, 0, 0, *this, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_STATUS)), nullptr, nullptr);
        navidrome::win::setFont(m_status, f);

        addDarkModeHooksKeepingLists(m_darkMode, *this);
        refresh();
        return 0;
    }

    void OnSize(UINT, CSize sz) {
        const navidrome::win::UiScale s(*this);
        const int btnH = (std::max)(s(26), m_lineH + s(10)), gap = s(8), pad = s(8);
        int listH = sz.cy - btnH - pad * 3;
        if (listH < 0) listH = 0;
        m_list.SetWindowPos(nullptr, pad, pad, sz.cx - pad * 2, listH, SWP_NOZORDER);
        int y = pad * 2 + listH;
        int x = pad;
        for (HWND b : { m_newBtn, m_editBtn, m_deleteBtn }) {
            int w = navidrome::win::fitWidth(b, s, 24, 90);
            ::SetWindowPos(b, nullptr, x, y, w, btnH, SWP_NOZORDER);
            x += w + gap;
        }
        ::SetWindowPos(m_status, nullptr, x, y + (btnH - m_lineH) / 2,
            (sz.cx - x - pad) > 0 ? sz.cx - x - pad : 0, m_lineH, SWP_NOZORDER);
    }

    void setStatus(const std::string& s) { ::SetWindowTextW(m_status, u8ToWide(s).c_str()); }

    void refresh() {
        if (!navidrome::SubsonicClientWin::get().isConfigured()) { setStatus("Not configured"); return; }
        setStatus("Loading…");
        std::thread([this]() {
            std::string err;
            auto stations = navidrome::SubsonicClientWin::get().getRadioStations(err);
            auto* payload = err.empty()
                ? new std::vector<navidrome::RadioStation>(std::move(stations))
                : nullptr;
            if (!PostMessage(WM_RADIO_LOADED, reinterpret_cast<WPARAM>(payload), 0))
                delete payload;   // window already gone
        }).detach();
    }

    LRESULT OnRadioLoaded(UINT, WPARAM wParam, LPARAM) {
        auto* stations = reinterpret_cast<std::vector<navidrome::RadioStation>*>(wParam);
        if (stations) {
            m_stations = std::move(*stations);
            delete stations;
            populateList();
            setStatus(m_stations.empty() ? "No radio stations" : "");
        } else {
            setStatus("Failed to load radio stations");
        }
        return 0;
    }

    void populateList() {
        m_list.DeleteAllItems();
        int i = 0;
        for (auto& s : m_stations) {
            m_list.InsertItem(i, u8ToWide(s.name).c_str());
            m_list.SetItemText(i, 1, u8ToWide(s.streamUrl).c_str());
            m_list.SetItemText(i, 2, u8ToWide(s.homePageUrl).c_str());
            ++i;
        }
    }

    int selectedIndex() { return m_list.GetNextItem(-1, LVNI_SELECTED); }

    void OnNew(UINT, int, HWND) {
        std::wstring name, streamUrl, homePageUrl;
        if (!RadioStationPromptWindow::run(*this, L"New Radio Station", L"", L"", L"",
                                           name, streamUrl, homePageUrl))
            return;
        std::string nameU8 = wToU8(name), urlU8 = wToU8(streamUrl), homeU8 = wToU8(homePageUrl);
        if (nameU8.empty() || urlU8.empty()) { setStatus("Name and stream URL are required"); return; }

        setStatus("Creating…");
        std::thread([this, nameU8, urlU8, homeU8]() {
            std::string err;
            std::string result = navidrome::SubsonicClientWin::get()
                                      .createRadioStation(urlU8, nameU8, homeU8, err);
            bool ok = err.empty();
            fb2k::inMainThread([this, ok, err]() {
                if (!IsWindow()) return;
                if (ok) refresh();
                else setStatus("Failed: " + (err.empty() ? "unknown error" : err));
            });
        }).detach();
    }

    void OnEdit(UINT, int, HWND) {
        int idx = selectedIndex();
        if (idx < 0 || static_cast<std::size_t>(idx) >= m_stations.size()) {
            setStatus("Select a station"); return;
        }
        navidrome::RadioStation station = m_stations[static_cast<std::size_t>(idx)];

        std::wstring name, streamUrl, homePageUrl;
        if (!RadioStationPromptWindow::run(*this, L"Edit Radio Station",
                                           u8ToWide(station.name), u8ToWide(station.streamUrl),
                                           u8ToWide(station.homePageUrl),
                                           name, streamUrl, homePageUrl))
            return;
        std::string nameU8 = wToU8(name), urlU8 = wToU8(streamUrl), homeU8 = wToU8(homePageUrl);
        if (nameU8.empty() || urlU8.empty()) { setStatus("Name and stream URL are required"); return; }

        std::string id = station.id;
        setStatus("Updating…");
        std::thread([this, id, nameU8, urlU8, homeU8]() {
            std::string err;
            bool ok = navidrome::SubsonicClientWin::get().updateRadioStation(id, urlU8, nameU8, homeU8, err);
            fb2k::inMainThread([this, ok, err]() {
                if (!IsWindow()) return;
                if (ok) refresh();
                else setStatus("Failed: " + (err.empty() ? "unknown error" : err));
            });
        }).detach();
    }

    void OnDelete(UINT, int, HWND) {
        int idx = selectedIndex();
        if (idx < 0 || static_cast<std::size_t>(idx) >= m_stations.size()) {
            setStatus("Select a station"); return;
        }
        navidrome::RadioStation station = m_stations[static_cast<std::size_t>(idx)];

        std::wstring prompt = L"Delete \"" + u8ToWide(station.name) + L"\" from the server?\r\n\r\n"
                              L"The station is removed for every client.";
        if (MessageBoxW(prompt.c_str(), L"Delete radio station",
                        MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2) != IDYES)
            return;

        std::string id = station.id;
        setStatus("Deleting…");
        std::thread([this, id]() {
            std::string err;
            bool ok = navidrome::SubsonicClientWin::get().deleteRadioStation(id, err);
            fb2k::inMainThread([this, ok, err]() {
                if (!IsWindow()) return;
                if (ok) refresh();
                else setStatus("Failed: " + (err.empty() ? "unknown error" : err));
            });
        }).detach();
    }

    CListViewCtrl m_list;
    HWND m_newBtn = nullptr, m_editBtn = nullptr, m_deleteBtn = nullptr, m_status = nullptr;
    int  m_lineH = 16;
    std::vector<navidrome::RadioStation> m_stations;
    fb2k::CCoreDarkModeHooks m_darkMode;
    preferences_page_callback::ptr m_cb;
};

class NavidromeRadioPrefsFactory : public preferences_page_v3 {
public:
    preferences_page_instance::ptr instantiate(HWND parent,
        preferences_page_callback::ptr cb) override {
        auto inst = fb2k::service_new<NavidromeRadioPrefsInstance>(cb);
        inst->Create(parent);
        return inst;
    }
    const char* get_name() override { return "Radio Stations"; }
    GUID        get_guid() override {
        return { 0xa1b2c3d4,0x1111,0x2222,{0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x01,0x0f} };
    }
    // Nested under the main Navidrome credentials page (guid_prefs_page,
    // tail 0x05), not guid_tools — makes this a child sub-page under
    // "Navidrome" rather than a sibling of it.
    GUID        get_parent_guid() override {
        return { 0xa1b2c3d4,0x1111,0x2222,{0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x01,0x05} };
    }
};
FB2K_SERVICE_FACTORY(NavidromeRadioPrefsFactory);

// ---------------------------------------------------------------------------
// Preferences > Media Library > Navidrome > Libraries — multi-library filter.
// Nested under the main Navidrome credentials page (guid_prefs_page, tail
// 0x05). A checkbox ("Only include selected libraries") bound to
// cfg_library_filter, plus a checkbox list of the server's getMusicFolders
// entries bound to cfg_library_ids. Standard apply model: edits are STAGED in
// m_stagedEnabled / m_selected and only written to cfg on apply() — Cancel
// (host destroys the page without apply()) discards them. Everything is inert
// until the server reports 2+ libraries — see navidrome::effectiveMusicFolderIds.
// ---------------------------------------------------------------------------
class NavidromeLibSelPrefsInstance : public CWindowImpl<NavidromeLibSelPrefsInstance>,
                                     public preferences_page_instance {
public:
    DECLARE_WND_CLASS(L"foo_navidrome_LibSelPrefsWnd")

    explicit NavidromeLibSelPrefsInstance(preferences_page_callback::ptr cb) : m_cb(cb) {}

    HWND     get_wnd() override { return m_hWnd; }
    t_uint32 get_state() override {
        return preferences_state::dark_mode_supported |
               (m_changed ? preferences_state::changed | preferences_state::resettable : 0);
    }
    void apply() override {
        navidrome::cfg_library_filter.set(m_stagedEnabled);
        navidrome::cfg_library_ids.set(navidrome::joinMusicFolderIds(m_selected).c_str());
        navidrome::SubsonicClientWin::get().refreshMusicFolders();
        m_savedEnabled = m_stagedEnabled;
        m_savedIds     = m_selected;
        m_changed      = false;
        notifyCb();
    }
    void reset() override {
        // "Reset page" restores the built-in defaults: filter off, no libraries.
        m_stagedEnabled = false;
        m_selected.clear();
        CheckDlgButton(IDC_ENABLE, BST_UNCHECKED);
        setChecksFromSelected();
        recomputeChanged();
        applyEnabledState();
    }

    enum { WM_FOLDERS_LOADED = WM_USER + 210 };

    BEGIN_MSG_MAP(NavidromeLibSelPrefsInstance)
        MSG_WM_ERASEBKGND(OnEraseBkgnd)
        MSG_WM_CREATE(OnCreate)
        MSG_WM_SIZE(OnSize)
        MESSAGE_HANDLER_EX(WM_FOLDERS_LOADED, OnFoldersLoaded)
        COMMAND_ID_HANDLER_EX(IDC_ENABLE, OnToggleEnable)
        NOTIFY_HANDLER_EX(IDC_LIST, LVN_ITEMCHANGED, OnListItemChanged)
    END_MSG_MAP()

    BOOL OnEraseBkgnd(CDCHandle dc) { navidrome::win::eraseLikeDialog(*this, dc); return TRUE; }

private:
    enum { IDC_ENABLE = 5101, IDC_LIST = 5102, IDC_STATUS = 5103 };

    void notifyCb() { if (m_cb.is_valid()) m_cb->on_state_changed(); }

    void recomputeChanged() {
        bool c = (m_stagedEnabled != m_savedEnabled) || (m_selected != m_savedIds);
        if (c != m_changed) { m_changed = c; notifyCb(); }
    }

    LRESULT OnCreate(LPCREATESTRUCT) {
        const navidrome::win::UiScale s(*this);
        HFONT f = navidrome::win::uiFont(*this);
        m_lineH = navidrome::win::lineHeight(*this, f);

        m_savedEnabled  = navidrome::cfg_library_filter.get();
        m_stagedEnabled = m_savedEnabled;
        m_savedIds = m_selected =
            navidrome::parseMusicFolderIds(navidrome::cfg_library_ids.get().c_str());

        m_enable = CreateWindowW(L"BUTTON", L"Only include selected libraries",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            0, 0, 0, 0, *this, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_ENABLE)), nullptr, nullptr);
        navidrome::win::setFont(m_enable, f);
        CheckDlgButton(IDC_ENABLE, m_stagedEnabled ? BST_CHECKED : BST_UNCHECKED);

        m_list.Create(*this, CWindow::rcDefault, nullptr,
            WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL | WS_TABSTOP | LVS_NOSORTHEADER,
            WS_EX_CLIENTEDGE, IDC_LIST);
        m_list.SetExtendedListViewStyle(LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT);
        m_list.InsertColumn(0, L"Library", LVCFMT_LEFT, s(320));
        m_list.SetFont(f);

        m_status = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_ENDELLIPSIS,
            0, 0, 0, 0, *this, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_STATUS)), nullptr, nullptr);
        navidrome::win::setFont(m_status, f);

        addDarkModeHooksKeepingLists(m_darkMode, *this);
        refresh();
        return 0;
    }

    void OnSize(UINT, CSize sz) {
        const navidrome::win::UiScale s(*this);
        const int pad = s(8), chkH = (std::max)(s(20), m_lineH + s(4)), statusH = m_lineH;
        ::SetWindowPos(m_enable, nullptr, pad, pad, sz.cx - pad * 2, chkH, SWP_NOZORDER);
        int listY = pad * 2 + chkH;
        int listH = sz.cy - listY - pad * 2 - statusH;
        if (listH < 0) listH = 0;
        m_list.SetWindowPos(nullptr, pad, listY, sz.cx - pad * 2, listH, SWP_NOZORDER);
        ::SetWindowPos(m_status, nullptr, pad, listY + listH + pad, sz.cx - pad * 2, statusH, SWP_NOZORDER);
    }

    void setStatus(const std::string& s) { ::SetWindowTextW(m_status, u8ToWide(s).c_str()); }

    void applyEnabledState() {
        bool multi = m_folders.size() >= 2;
        ::EnableWindow(m_enable, multi);
        ::EnableWindow(m_list, m_stagedEnabled && multi);
    }

    void refresh() {
        if (!navidrome::SubsonicClientWin::get().isConfigured()) { setStatus("Not configured"); return; }
        setStatus("Loading…");
        std::thread([this]() {
            std::string err;
            auto folders = navidrome::SubsonicClientWin::get().getMusicFolders(err);
            auto* payload = err.empty()
                ? new std::vector<navidrome::MusicFolder>(std::move(folders))
                : nullptr;
            if (!PostMessage(WM_FOLDERS_LOADED, reinterpret_cast<WPARAM>(payload), 0))
                delete payload;   // window already gone
        }).detach();
    }

    LRESULT OnFoldersLoaded(UINT, WPARAM wParam, LPARAM) {
        auto* folders = reinterpret_cast<std::vector<navidrome::MusicFolder>*>(wParam);
        if (!folders) { setStatus("Failed to load libraries"); return 0; }
        m_folders = std::move(*folders);
        delete folders;
        setChecksFromSelected();
        setStatus(m_folders.size() < 2
            ? "This server reports a single library — nothing to filter."
            : "");
        applyEnabledState();
        return 0;
    }

    // Rebuild the list rows and their check state from m_folders + m_selected.
    void setChecksFromSelected() {
        m_populating = true;
        m_list.DeleteAllItems();
        int i = 0;
        for (auto& mf : m_folders) {
            m_list.InsertItem(i, u8ToWide(mf.name.empty() ? mf.id : mf.name).c_str());
            m_list.SetCheckState(i,
                std::find(m_selected.begin(), m_selected.end(), mf.id) != m_selected.end());
            ++i;
        }
        m_populating = false;
    }

    // Rebuild m_selected from the rows the user has checked (folder order).
    void readSelectionFromChecks() {
        std::vector<std::string> ids;
        for (int i = 0; i < static_cast<int>(m_folders.size()); ++i)
            if (m_list.GetCheckState(i)) ids.push_back(m_folders[static_cast<std::size_t>(i)].id);
        m_selected = std::move(ids);
    }

    void OnToggleEnable(UINT, int, HWND) {
        m_stagedEnabled = IsDlgButtonChecked(IDC_ENABLE) == BST_CHECKED;
        if (!m_stagedEnabled) {
            // Turning the filter off clears the staged selection rather than
            // parking it — the row checks go with it. Nothing is written until
            // apply().
            m_selected.clear();
            setChecksFromSelected();
        }
        recomputeChanged();
        applyEnabledState();
    }

    LRESULT OnListItemChanged(NMHDR* h) {
        auto* nm = reinterpret_cast<NMLISTVIEW*>(h);
        if (!m_populating && (nm->uChanged & LVIF_STATE) &&
            ((nm->uOldState ^ nm->uNewState) & LVIS_STATEIMAGEMASK)) {
            readSelectionFromChecks();
            recomputeChanged();
        }
        return 0;
    }

    CListViewCtrl m_list;
    HWND m_enable = nullptr, m_status = nullptr;
    int  m_lineH = 16;
    fb2k::CCoreDarkModeHooks m_darkMode;
    std::vector<navidrome::MusicFolder> m_folders;
    std::vector<std::string> m_selected;    // staged selection
    std::vector<std::string> m_savedIds;    // cfg value at page open / last apply
    bool m_stagedEnabled = false;
    bool m_savedEnabled  = false;
    bool m_changed       = false;
    bool m_populating    = false;
    preferences_page_callback::ptr m_cb;
};

class NavidromeLibSelPrefsFactory : public preferences_page_v3 {
public:
    preferences_page_instance::ptr instantiate(HWND parent,
        preferences_page_callback::ptr cb) override {
        auto inst = fb2k::service_new<NavidromeLibSelPrefsInstance>(cb);
        inst->Create(parent);
        return inst;
    }
    const char* get_name() override { return "Libraries"; }
    GUID        get_guid() override {
        return { 0xa1b2c3d4,0x1111,0x2222,{0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x01,0x12} };
    }
    GUID        get_parent_guid() override {
        return { 0xa1b2c3d4,0x1111,0x2222,{0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x01,0x05} };
    }
};
FB2K_SERVICE_FACTORY(NavidromeLibSelPrefsFactory);

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
        const navidrome::win::UiScale s(*this);
        SetWindowPos(nullptr, 0, 0, s(580), s(660),
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
    HFONT hFont = navidrome::win::uiFont(*this);
    m_lineH = navidrome::win::lineHeight(*this, hFont);

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
    ::SetWindowSubclass(m_tree, &BrowserWindow::TreeSubclassProc, 1,
                        reinterpret_cast<DWORD_PTR>(this));

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
    KillTimer(kSearchDebounceTimer);
    ::RemoveWindowSubclass(m_tree, &BrowserWindow::TreeSubclassProc, 1);
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

    m_search.SetWindowPos(nullptr,
        pad, pad, w - 2*pad, searchH,
        SWP_NOZORDER);
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

// ---------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------
// Smart-list roots (Starred, Recently Added, ... Radio) \u2014 shared with macOS,
// see navidrome::buildCategoryNodes() in NavidromeBrowserModel.h.
using navidrome::buildCategoryNodes;

// Adapts the Windows Subsonic client singleton to the platform-neutral
// IBrowserClient seam the shared tree logic (buildRootNodes / fetchChildren /
// collectSongsDeep) is written against. Stateless \u2014 every call forwards to
// SubsonicClientWin::get().
namespace {
struct WinBrowserClient final : navidrome::IBrowserClient {
    navidrome::SubsonicClientWin& c = navidrome::SubsonicClientWin::get();

    std::vector<navidrome::Artist> getArtists(std::string& e) override {
        return c.getArtists(e); }
    std::vector<navidrome::Artist> getArtistsForLibrary(const std::string& id,
                                                        std::string& e) override {
        return c.getArtistsForLibrary(id, e); }
    std::vector<navidrome::Album> getAlbumsForArtist(const std::string& id,
                                                     const std::string& scope,
                                                     std::string& e) override {
        return c.getAlbumsForArtist(id, e, scope); }
    std::vector<navidrome::Song> getSongsForAlbum(const std::string& id,
                                                  std::string& e) override {
        return c.getSongsForAlbum(id, e); }
    std::vector<navidrome::Song> getPlaylistSongs(const std::string& id,
                                                  std::string& e) override {
        return c.getPlaylistSongs(id, e); }
    std::vector<navidrome::Song> getSongsForGenre(const std::string& g, int n,
                                                  std::string& e) override {
        return c.getSongsForGenre(g, n, e); }
    std::vector<navidrome::Song> getStarredSongs(std::string& e) override {
        return c.getStarredSongs(e); }
    std::vector<navidrome::Genre> getGenres(std::string& e) override {
        return c.getGenres(e); }
    std::vector<navidrome::Playlist> getPlaylists(std::string& e) override {
        return c.getPlaylists(e); }
    std::vector<navidrome::Album> getAlbumList(navidrome::AlbumListType t, int n,
                                               std::string& e) override {
        return c.getAlbumList(t, n, e); }
    std::vector<navidrome::RadioStation> getRadioStations(std::string& e) override {
        return c.getRadioStations(e); }
    std::vector<navidrome::Bookmark> getBookmarks(std::string& e) override {
        return c.getBookmarks(e); }
    std::vector<navidrome::PodcastChannel> getPodcastChannels(std::string& e) override {
        return c.getPodcastChannels(e); }
    std::vector<navidrome::PodcastEpisode> getPodcastEpisodes(const std::string& id,
                                                               std::string& e) override {
        return c.getPodcastEpisodes(id, e); }
    std::vector<navidrome::NowPlayingEntry> getNowPlaying(std::string& e) override {
        return c.getNowPlaying(e); }
    std::vector<navidrome::Song> getSimilarSongs(const std::string& id, int n,
                                                 std::string& e) override {
        return c.getSimilarSongs(id, n, e); }
    std::vector<navidrome::Song> getRandomSongs(int n, std::string& e) override {
        return c.getRandomSongs(n, e); }
    std::vector<navidrome::Song> getAllSongs(std::string& e) override {
        return c.getAllSongs(e); }
    navidrome::ArtistInfo getArtistInfo(const std::string& id, std::string& e) override {
        return c.getArtistInfo(id, e); }
    std::vector<navidrome::Song> getTopSongs(const std::string& name, int n,
                                             std::string& e) override {
        return c.getTopSongs(name, n, e); }
    navidrome::Lyrics getLyrics(const std::string& id, const std::string& artist,
                                const std::string& title, std::string& e) override {
        return c.getLyrics(id, artist, title, e); }
    std::vector<std::string> groupingLibraryIds() override {
        return c.libraryGroupingIds(); }
    std::vector<navidrome::MusicFolder> musicFolders() override {
        return c.cachedMusicFolders(); }
    bool setStarred(bool starred, const std::string& id, navidrome::StarKind kind,
                    std::string& e) override {
        return c.setStarred(starred, id, kind, e); }
    bool setRating(int stars, const std::string& id, std::string& e) override {
        return c.setRating(stars, id, e); }
    bool getSong(const std::string& id, navidrome::Song& out, std::string& e) override {
        return c.getSong(id, out, e); }
};

navidrome::IBrowserClient& browserClient() {
    static WinBrowserClient inst;
    return inst;
}
} // namespace

// The library service's client (NavidromeLibraryPlatform.h) is the browser's own.
navidrome::IBrowserClient& navidrome::libraryClient() { return browserClient(); }

// The AudioMuse-AI prompts (NavidromeAudioMuse.h) reuse the browser's text prompt.
bool navidrome::promptForText(const char* title, const char* label, std::string& inOut) {
    std::wstring value;
    if (!TextPromptWindow::run(core_api::get_main_window(), u8ToWide(title).c_str(),
                               u8ToWide(label).c_str(), u8ToWide(inOut), value))
        return false;
    inOut = wToU8(value);
    return true;
}

void BrowserWindow::loadArtists() {
    // Any full reload supersedes whatever search was pending/showing.
    KillTimer(kSearchDebounceTimer);
    ++m_searchGeneration;
    m_isSearching = false;
    m_searchResultNodes.clear();

    setStatus("Loading artists\u2026");
    m_tree.DeleteAllItems();
    m_nodeMap.clear();
    m_rootNodes.clear();
    // Warm the cache the "Add to Navidrome Playlist" submenu reads from, so the
    // first right-click already lists the server's playlists.
    refreshServerPlaylists();
    refreshRadioStations();

    std::thread([this]() {
        auto* payload = new LoadedPayload{};
        // Categories + either per-library nodes (multi-library server) or a flat
        // artist list — the whole decision is shared with macOS.
        payload->nodes = navidrome::buildRootNodes(browserClient(), payload->error);
        PostMessage(WM_NAVIDROME_LOADED, reinterpret_cast<WPARAM>(payload), 0);
    }).detach();
}

// syncBrowserNodesToPlaylists (the search + rate/star rating push-back) is
// shared with macOS — see NavidromeBrowserModel.h.
using navidrome::syncBrowserNodesToPlaylists;

// ---------------------------------------------------------------------------
// Child fetch (synchronous \u2014 background thread only)
// ---------------------------------------------------------------------------
// The node-type dispatch, the "N tracks" subtitles and the playlist rating
// push-back all live in navidrome::fetchChildren, shared with macOS.
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
    // Superseded by a later keystroke/clear while this request was in
    // flight -- drop it instead of clobbering whatever's now on screen.
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

LRESULT BrowserWindow::OnNavidromeRadio(UINT, WPARAM wParam, LPARAM, BOOL&) {
    auto* stations = reinterpret_cast<std::vector<navidrome::RadioStation>*>(wParam);
    m_radioLoading = false;
    if (stations) { m_radioStations = std::move(*stations); delete stations; }
    return 0;
}

// Refresh the cached station list the enqueue path resolves streamUrl from.
// Cheap enough to re-run after every mutation, same as refreshServerPlaylists.
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
            delete payload;   // window already gone
    }).detach();
}

std::string BrowserWindow::radioStationURL(const std::string& stationId) {
    for (auto& s : m_radioStations)
        if (s.id == stationId) return s.streamUrl;
    return "";
}

void BrowserWindow::populateRoot(LoadedPayload* payload) {
    if (!payload->error.empty()) {
        setStatus("Error: " + payload->error); return;
    }
    m_rootNodes = payload->nodes;
    std::size_t artists = 0, libraries = 0;
    for (auto& n : m_rootNodes) {
        insertNode(TVI_ROOT, n);
        if (n->type == NavidromeNode::Artist)  ++artists;
        if (n->type == NavidromeNode::Library) ++libraries;
    }
    if (libraries) setStatus(std::to_string(libraries) + " libraries");
    else           setStatus(std::to_string(artists) + " artists");
}

// Renders m_searchResultNodes in place of the browse tree. m_rootNodes is
// left untouched so category invalidation and the startup rating refresh
// keep working against the real tree even while a search is on screen.
void BrowserWindow::populateSearchResults(LoadedPayload* payload) {
    if (!payload->error.empty()) {
        setStatus("Search error: " + payload->error); return;
    }
    m_isSearching       = true;
    m_searchResultNodes = payload->nodes;
    m_tree.DeleteAllItems();
    m_nodeMap.clear();
    for (auto& n : m_searchResultNodes) insertNode(TVI_ROOT, n);
    setStatus(std::to_string(m_searchResultNodes.size()) + " songs found");
}

// Re-renders the browse tree from m_rootNodes without a network round-trip.
// Any node that had children expanded before the search wiped the tree gets
// childrenLoaded reset so it lazily refetches on next expand -- cheap, and
// avoids re-inserting HTREEITEMs the tree already discarded.
void BrowserWindow::restoreBrowseTree() {
    m_isSearching = false;
    m_searchResultNodes.clear();
    m_tree.DeleteAllItems();
    m_nodeMap.clear();
    std::size_t artists = 0, libraries = 0;
    for (auto& n : m_rootNodes) {
        n->children.clear();
        n->childrenLoaded = false;
        SetNodeItem(n, nullptr);
        insertNode(TVI_ROOT, n);
        if (n->type == NavidromeNode::Artist)  ++artists;
        if (n->type == NavidromeNode::Library) ++libraries;
    }
    if (libraries) setStatus(std::to_string(libraries) + " libraries");
    else           setStatus(std::to_string(artists) + " artists");
}

void BrowserWindow::populateChildren(LoadedPayload* payload) {
    auto parent = payload->parent;
    if (!parent) return;

    // Remove placeholder "Loading..." item
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
        errNode->displayName = "Error: " + payload->error;
        parent->children     = { errNode };
    }

    for (auto& child : parent->children)
        insertNode(NodeItem(parent), child);

    if (parent->children.empty()) {
        // No children — clear the expand button. WTL's CTreeViewCtrl has no
        // SetItemChildren; set cChildren via the TVITEM mask directly.
        TVITEM it   = {};
        it.mask     = TVIF_CHILDREN;
        it.hItem    = NodeItem(parent);
        it.cChildren = 0;
        m_tree.SetItem(&it);
    }
}

// Tree label: track number, favorite marker, rating stars and bookmark
// position all live in the one item text — a treeview has no extra columns.
// The formatting is shared with macOS (which splits it back across its 3
// columns) — see navidrome::nodeDisplay() / singleColumnLabel().
std::string BrowserWindow::labelFor(const std::shared_ptr<NavidromeNode>& node) const {
    std::string label = navidrome::singleColumnLabel(*node);
    // Search results are a flat song list, so name the artist on each row.
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
    // Expand arrow on everything but leaves ("All Songs" included — it's
    // enqueue-only, see navidrome::isLeaf).
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

// ---------------------------------------------------------------------------
// Button actions
// ---------------------------------------------------------------------------
// Gather the tree's selected, playable nodes in tree order (see the
// multi-select notes in BrowserWindow.h). Items inside a collapsed parent are
// skipped — what isn't on screen isn't part of the selection.
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

// ---------------------------------------------------------------------------
// Multi-select
// ---------------------------------------------------------------------------
// TVGN_FIRSTVISIBLE is the first item *on screen* (scroll-dependent), so walk
// from the root instead: TVGN_NEXTVISIBLE follows every expanded branch.
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
    if (a == items.end()) a = b;   // anchor scrolled into a collapsed branch
    if (b == items.end()) return;
    if (a > b) std::swap(a, b);
    for (auto it = items.begin(); it != items.end(); ++it) {
        bool in = it >= a && it <= b;
        if (in != isSelected(*it))
            m_tree.SetItemState(*it, in ? TVIS_SELECTED : 0, TVIS_SELECTED);
    }
}

// Mouse half of multi-select. Clicks on the expand button, or anywhere off an
// item, keep the default behaviour.
bool BrowserWindow::onTreeLButtonDown(LPARAM lParam) {
    TVHITTESTINFO ht = {};
    // Signed client coords (GET_X_LPARAM without <windowsx.h>, whose
    // SubclassWindow macro clobbers ATL's CWindowImpl::SubclassWindow).
    ht.pt = { static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)) };
    HTREEITEM hit = m_tree.HitTest(&ht);
    if (!hit || !(ht.flags & TVHT_ONITEM)) return false;

    const bool ctrl  = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool shift = (::GetKeyState(VK_SHIFT)   & 0x8000) != 0;
    if (!ctrl && !shift) {
        // Plain click: drop the extras now — clicking the caret item itself
        // fires no TVN_SELCHANGED to do it later.
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

// Keyboard half: the caret moved (arrows, or a plain click). Shift extends a
// range from the anchor; anything else collapses the selection to the caret.
LRESULT BrowserWindow::OnTreeSelChanged(LPNMHDR pnmh) {
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

// Resolve the selected nodes to songs on a background thread, then enqueue on
// the main thread. closeAfter hides the window once the tracks are queued \u2014
// used by the Enter shortcut so "select artist + Enter" queues and dismisses.
void BrowserWindow::queueSelected(bool play, bool closeAfter, bool clearFirst) {
    queueNodes(selectedNodes(), play, closeAfter, clearFirst);
}

void BrowserWindow::queueNodes(std::vector<std::shared_ptr<NavidromeNode>> selected,
                               bool play, bool closeAfter, bool clearFirst) {
    if (selected.empty()) { setStatus("Select at least one item"); return; }

    NAVIDROME_LOG("UI", "queueNodes: " + std::to_string(selected.size()) +
                  " selected node(s), play=" + (play ? "1" : "0"));
    setStatus("Loading tracks\u2026");
    std::thread([this, selected, play, closeAfter, clearFirst]() {
        auto songs = navidrome::collectSelectionSongs(browserClient(), selected);
        fb2k::inMainThread([this, songs, play, closeAfter, clearFirst]() mutable {
            enqueueNodes(std::move(songs), play, clearFirst);
            if (closeAfter && !m_embedded && IsWindow()) ShowWindow(SW_HIDE);
        });
    }).detach();
}

void BrowserWindow::OnAdd(UINT, int, HWND)  { dbgLog("OnAdd fired"); queueSelected(false, false); }
void BrowserWindow::OnPlay(UINT, int, HWND) { dbgLog("OnPlay fired"); queueSelected(true,  false); }

// Instant Mix from the first selected artist, album or song — the shared run
// in main.cpp (progress window, dedicated "Instant Mix" playlist).
void BrowserWindow::OnPlaySimilar(UINT, int, HWND) {
    dbgLog("OnPlaySimilar (Instant Mix) fired");
    auto selected = selectedNodes();
    auto node = selected.empty() ? nullptr : selected.front();
    if (!node || !navidrome::isSimilarEligible(*node)) {
        setStatus("Instant Mix needs an artist, album, or song");
        return;
    }
    navidrome::startInstantMix(node);
}

// Song Alchemy over the selected songs/artists — the run itself (progress
// window, new playlist) is the shared one in main.cpp.
void BrowserWindow::OnAlchemy(UINT, int, HWND) {
    dbgLog("OnAlchemy fired");
    std::string label;
    auto seeds = navidrome::audiomuse::seedsFromNodes(selectedNodes(), label);
    if (seeds.empty()) { setStatus("Song Alchemy needs songs or artists"); return; }
    navidrome::audioMuseAlchemy(std::move(seeds), std::move(label));
}

// Fetches the selected artist's biography + last.fm link and shows it in a
// plain message box — a read-only lookup, not an enqueue action, so it skips
// the enqueueNodes path every other context-menu action here goes through.
void BrowserWindow::OnArtistInfo(UINT, int, HWND) {
    dbgLog("OnArtistInfo fired");
    auto selected = selectedNodes();
    auto node = selected.empty() ? nullptr : selected.front();
    if (!node || node->type != navidrome::BrowserNode::Artist) {
        setStatus("Artist Info needs an artist");
        return;
    }

    setStatus("Fetching artist info…");
    std::string artistId   = node->id;
    std::wstring artistName = u8ToWide(node->displayName);
    std::thread([this, artistId, artistName]() {
        std::string err;
        auto info = navidrome::SubsonicClientWin::get().getArtistInfo(artistId, err);
        std::wstring text = u8ToWide(navidrome::formatArtistBiography(info));
        fb2k::inMainThread([this, artistName, text, err]() mutable {
            if (!IsWindow()) return;
            if (!err.empty()) { setStatus("Error: " + err); return; }
            setStatus("");
            MessageBoxW(text.c_str(), artistName.c_str(), MB_OK | MB_ICONINFORMATION);
        });
    }).detach();
}

// Fetches a fresh batch of random tracks and appends + plays them. No
// selection needed — always available, like "Send Active Playlist".
void BrowserWindow::OnRandomMix(UINT, int, HWND) {
    dbgLog("OnRandomMix fired");
    setStatus("Fetching random mix…");
    std::thread([this]() {
        std::string err;
        auto nodes = navidrome::fetchRandomMix(browserClient(), 100, err);
        fb2k::inMainThread([this, nodes, err]() mutable {
            if (!IsWindow()) return;
            if (!err.empty()) { setStatus("Error: " + err); return; }
            if (nodes.empty()) { setStatus("No tracks found"); return; }
            enqueueNodes(std::move(nodes), true, false);
        });
    }).detach();
}

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
    dbgLog("OnContextMenu: wnd=" + std::to_string(reinterpret_cast<uintptr_t>(wnd.m_hWnd)) +
           " tree=" + std::to_string(reinterpret_cast<uintptr_t>(m_tree.m_hWnd)) +
           " point=" + std::to_string(point.x) + "," + std::to_string(point.y));
    if (wnd.m_hWnd != m_tree.m_hWnd) {
        dbgLog("OnContextMenu: wnd mismatch, passing through");
        SetMsgHandled(FALSE); return;
    }
    if (m_passContextMenu && m_passContextMenu()) {
        // Layout edit mode: hand the click to the Default UI host.
        GetParent().SendMessage(WM_CONTEXTMENU, reinterpret_cast<WPARAM>(wnd.m_hWnd),
                                MAKELPARAM(point.x, point.y));
        return;
    }

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
        // Right-click inside a multi-selection acts on all of it; outside, it
        // selects just the clicked row (SelectItem -> OnTreeSelChanged clears).
        if (hit && !isSelected(hit)) m_tree.SelectItem(hit);
    }

    auto selForMenu = selectedNodes();
    dbgLog("OnContextMenu: selectedNodes count=" + std::to_string(selForMenu.size()));
    if (selForMenu.empty()) { dbgLog("OnContextMenu: empty selection, aborting"); return; }

    CMenu menu;
    menu.CreatePopupMenu();
    menu.AppendMenu(MF_STRING, IDC_PLAY, L"Play Now");
    menu.AppendMenu(MF_STRING, IDC_ADD,  L"Add to Playlist");
    menu.AppendMenu(MF_STRING, IDC_PLAY_SIMILAR, L"Instant Mix");
    if (navidrome::audioMuseSettings().configured())
        menu.AppendMenu(MF_STRING, IDC_ALCHEMY, L"Song Alchemy (AudioMuse-AI)");
    menu.AppendMenu(MF_STRING, IDC_ARTIST_INFO,  L"Artist Info");

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

    // Internet radio stations. Unlike playlists, "New" needs no selection.
    menu.AppendMenu(MF_SEPARATOR);
    menu.AppendMenu(MF_STRING, IDC_NEW_RADIO,    L"New Radio Station…");
    menu.AppendMenu(MF_STRING, IDC_EDIT_RADIO,   L"Edit Radio Station…");
    menu.AppendMenu(MF_STRING, IDC_DELETE_RADIO, L"Delete Radio Station…");

    // Podcast channels. Like radio, "Subscribe" needs no selection.
    menu.AppendMenu(MF_SEPARATOR);
    menu.AppendMenu(MF_STRING, IDC_SUBSCRIBE_PODCAST,   L"Subscribe to Podcast…");
    menu.AppendMenu(MF_STRING, IDC_UNSUBSCRIBE_PODCAST, L"Unsubscribe from Podcast…");

    menu.AppendMenu(MF_SEPARATOR);
    menu.AppendMenu(MF_STRING, IDC_SEND_PLAYLIST,
                    L"Send Active Playlist to Navidrome");
    menu.AppendMenu(MF_STRING, IDC_RANDOM_MIX, L"Random Mix");
    menu.AppendMenu(MF_STRING, IDC_DOWNLOAD, L"Download Original Files…");

    dbgLog("OnContextMenu: showing TrackPopupMenu");
    menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y, *this);
    dbgLog("OnContextMenu: TrackPopupMenu returned");

    // A stale cache is only visible once — refresh for the next open.
    refreshServerPlaylists();
    refreshRadioStations();
}

// ---------------------------------------------------------------------------
// Favorites, ratings and playlist upload
// ---------------------------------------------------------------------------
void BrowserWindow::OnStar(UINT, int, HWND)   { dbgLog("OnStar fired");   applyStarred(true); }
void BrowserWindow::OnUnstar(UINT, int, HWND) { dbgLog("OnUnstar fired"); applyStarred(false); }

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
        auto result = navidrome::applyStarredToNodes(browserClient(), targets, starred);
        if (!result.error.empty())
            NAVIDROME_WARN("UI", std::string(starred ? "star" : "unstar") +
                " failed: " + result.error);
        fb2k::inMainThread([this, targets, starred, result]() {
            if (!IsWindow()) return;
            for (auto& n : targets) refreshLabel(n);
            setStatus(result.error.empty()
                ? (starred ? "Starred " : "Unstarred ") + std::to_string(result.done) + " item(s)"
                : "Error: " + result.error);
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
    dbgLog("OnRate fired: id=" + std::to_string(id) + " stars=" + std::to_string(stars));
    if (stars < 0 || stars > 5) { dbgLog("OnRate: stars out of range, aborting"); return; }

    std::vector<std::shared_ptr<NavidromeNode>> songs;
    for (auto& n : selectedNodes())
        if (n->type == NavidromeNode::Song) songs.push_back(n);
    dbgLog("OnRate: selected song count=" + std::to_string(songs.size()));
    if (songs.empty()) { dbgLog("OnRate: no song-type nodes selected, aborting"); setStatus("Select one or more songs to rate"); return; }

    std::thread([this, songs, stars]() {
        auto result = navidrome::applyRatingToNodes(browserClient(), songs, stars);
        if (!result.error.empty())
            NAVIDROME_WARN("UI", "OnRate: stars=" + std::to_string(stars) +
                " failed: " + result.error);
        fb2k::inMainThread([this, songs, result]() {
            if (!IsWindow()) return;
            for (auto& n : songs) refreshLabel(n);
            setStatus(result.error.empty()
                ? "Rated " + std::to_string(songs.size()) + " song(s)"
                : "Error: " + result.error);
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
        if (!ok) NAVIDROME_WARN("UI", "OnSendActivePlaylist \"" + name + "\" failed: " + err);
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
        auto songs = navidrome::collectSelectionSongs(browserClient(), selected);

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
    return navidrome::collectSongIdsDeep(browserClient(), nodes);
}

std::shared_ptr<NavidromeNode> BrowserWindow::singleSelectedPlaylist() {
    auto sel = selectedNodes();
    if (sel.size() != 1 || sel[0]->type != NavidromeNode::Playlist) return nullptr;
    return sel[0];
}

// Drop a node's cached children (and their tree items) so the next expand
// refetches from the server.
void BrowserWindow::reloadNodeChildren(const std::shared_ptr<NavidromeNode>& node) {
    if (!node || !NodeItem(node)) return;

    m_tree.Expand(NodeItem(node), TVE_COLLAPSE);
    HTREEITEM child = m_tree.GetChildItem(NodeItem(node));
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
    it.hItem     = NodeItem(node);
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
        if (!ok) NAVIDROME_WARN("UI", "OnAddToServerPlaylist \"" + name + "\" failed: " + err);
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
        if (!ok) NAVIDROME_WARN("UI", "OnNewServerPlaylist \"" + nameU8 + "\" failed: " + err);
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
        if (n->type != NavidromeNode::Song || !NodeItem(n)) continue;
        auto parent = nodeForItem(m_tree.GetParentItem(NodeItem(n)));
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
        if (!ok) NAVIDROME_WARN("UI", "OnRemoveFromPlaylist \"" + name + "\" failed: " + err);
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
        if (!ok) NAVIDROME_WARN("UI", "OnRenamePlaylist -> \"" + nameU8 + "\" failed: " + err);
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
        if (!ok) NAVIDROME_WARN("UI", "OnDeletePlaylist \"" + name + "\" failed: " + err);
        fb2k::inMainThread([this, name, ok, err]() {
            if (!IsWindow()) return;
            setStatus(ok ? "Deleted \"" + name + "\""
                         : "Failed: " + (err.empty() ? "unknown error" : err));
            if (ok) { invalidatePlaylistsCategory(); refreshServerPlaylists(); }
        });
    }).detach();
}

// ---------------------------------------------------------------------------
// Radio station management
// ---------------------------------------------------------------------------
std::shared_ptr<NavidromeNode> BrowserWindow::singleSelectedRadioStation() {
    auto sel = selectedNodes();
    if (sel.size() != 1 || sel[0]->type != NavidromeNode::Radio) return nullptr;
    return sel[0];
}

void BrowserWindow::invalidateRadioCategory() {
    for (auto& root : m_rootNodes) {
        if (root->type == NavidromeNode::Category &&
            root->category == NavidromeNode::CatRadio) {
            reloadNodeChildren(root);
            return;
        }
    }
}

// Unlike a new playlist, creating a station needs no selection.
void BrowserWindow::OnNewRadioStation(UINT, int, HWND) {
    std::wstring name, streamUrl, homePageUrl;
    if (!RadioStationPromptWindow::run(*this, L"New Radio Station", L"", L"", L"",
                                       name, streamUrl, homePageUrl))
        return;
    std::string nameU8 = wToU8(name), urlU8 = wToU8(streamUrl), homeU8 = wToU8(homePageUrl);
    if (nameU8.empty() || urlU8.empty()) {
        setStatus("Name and stream URL are required");
        return;
    }

    setStatus("Creating radio station…");
    std::thread([this, nameU8, urlU8, homeU8]() {
        std::string err;
        std::string result = navidrome::SubsonicClientWin::get()
                                  .createRadioStation(urlU8, nameU8, homeU8, err);
        bool ok = err.empty();
        fb2k::inMainThread([this, nameU8, ok, err]() {
            if (!IsWindow()) return;
            setStatus(ok ? "Created \"" + nameU8 + "\""
                         : "Failed: " + (err.empty() ? "unknown error" : err));
            if (ok) { invalidateRadioCategory(); refreshRadioStations(); }
        });
    }).detach();
}

void BrowserWindow::OnEditRadioStation(UINT, int, HWND) {
    auto node = singleSelectedRadioStation();
    if (!node) { setStatus("Select a single radio station"); return; }

    std::string currentUrl = radioStationURL(node->id);
    std::wstring name, streamUrl, homePageUrl;
    if (!RadioStationPromptWindow::run(*this, L"Edit Radio Station",
                                       u8ToWide(node->displayName),
                                       u8ToWide(currentUrl),
                                       u8ToWide(node->subtitle),
                                       name, streamUrl, homePageUrl))
        return;
    std::string nameU8 = wToU8(name), urlU8 = wToU8(streamUrl), homeU8 = wToU8(homePageUrl);
    if (nameU8.empty() || urlU8.empty()) {
        setStatus("Name and stream URL are required");
        return;
    }

    const std::string stationId = node->id;
    std::thread([this, stationId, nameU8, urlU8, homeU8]() {
        std::string err;
        bool ok = navidrome::SubsonicClientWin::get()
                      .updateRadioStation(stationId, urlU8, nameU8, homeU8, err);
        fb2k::inMainThread([this, nameU8, ok, err]() {
            if (!IsWindow()) return;
            setStatus(ok ? "Updated \"" + nameU8 + "\""
                         : "Failed: " + (err.empty() ? "unknown error" : err));
            if (ok) { invalidateRadioCategory(); refreshRadioStations(); }
        });
    }).detach();
}

void BrowserWindow::OnDeleteRadioStation(UINT, int, HWND) {
    auto node = singleSelectedRadioStation();
    if (!node) { setStatus("Select a single radio station"); return; }

    std::wstring prompt = L"Delete \"" + u8ToWide(node->displayName) +
                          L"\" from the server?\r\n\r\n"
                          L"The station is removed for every client.";
    if (MessageBoxW(prompt.c_str(), L"Delete radio station",
                    MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2) != IDYES)
        return;

    const std::string stationId = node->id;
    const std::string name      = node->displayName;
    std::thread([this, stationId, name]() {
        std::string err;
        bool ok = navidrome::SubsonicClientWin::get().deleteRadioStation(stationId, err);
        fb2k::inMainThread([this, name, ok, err]() {
            if (!IsWindow()) return;
            setStatus(ok ? "Deleted \"" + name + "\""
                         : "Failed: " + (err.empty() ? "unknown error" : err));
            if (ok) { invalidateRadioCategory(); refreshRadioStations(); }
        });
    }).detach();
}

// ---------------------------------------------------------------------------
// Podcast channel management
// ---------------------------------------------------------------------------
std::shared_ptr<NavidromeNode> BrowserWindow::singleSelectedPodcastChannel() {
    auto sel = selectedNodes();
    if (sel.size() != 1 || sel[0]->type != NavidromeNode::PodcastChannel) return nullptr;
    return sel[0];
}

void BrowserWindow::invalidatePodcastsCategory() {
    for (auto& root : m_rootNodes) {
        if (root->type == NavidromeNode::Category &&
            root->category == NavidromeNode::CatPodcasts) {
            reloadNodeChildren(root);
            return;
        }
    }
}

// Unlike editing a radio station, Subsonic's podcast API has no update
// endpoint — only subscribe (create) and unsubscribe (delete).
void BrowserWindow::OnSubscribePodcast(UINT, int, HWND) {
    std::wstring url;
    if (!TextPromptWindow::run(*this, L"Subscribe to Podcast",
                               L"Podcast RSS feed URL:", L"", url))
        return;
    std::string urlU8 = wToU8(url);
    if (urlU8.empty()) { setStatus("A feed URL is required"); return; }

    setStatus("Subscribing…");
    std::thread([this, urlU8]() {
        std::string err;
        navidrome::SubsonicClientWin::get().createPodcastChannel(urlU8, err);
        bool ok = err.empty();
        if (!ok) NAVIDROME_WARN("UI", "OnSubscribePodcast \"" + urlU8 + "\" failed: " + err);
        fb2k::inMainThread([this, ok, err]() {
            if (!IsWindow()) return;
            setStatus(ok ? "Subscribed" : "Failed: " + (err.empty() ? "unknown error" : err));
            if (ok) invalidatePodcastsCategory();
        });
    }).detach();
}

void BrowserWindow::OnUnsubscribePodcast(UINT, int, HWND) {
    auto node = singleSelectedPodcastChannel();
    if (!node) { setStatus("Select a single podcast"); return; }

    std::wstring prompt = L"Unsubscribe from \"" + u8ToWide(node->displayName) +
                          L"\"?\r\n\r\nDownloaded episodes are removed from the server.";
    if (MessageBoxW(prompt.c_str(), L"Unsubscribe from podcast",
                    MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2) != IDYES)
        return;

    const std::string channelId = node->id;
    const std::string name      = node->displayName;
    std::thread([this, channelId, name]() {
        std::string err;
        bool ok = navidrome::SubsonicClientWin::get().deletePodcastChannel(channelId, err);
        if (!ok) NAVIDROME_WARN("UI", "OnUnsubscribePodcast \"" + name + "\" failed: " + err);
        fb2k::inMainThread([this, name, ok, err]() {
            if (!IsWindow()) return;
            setStatus(ok ? "Unsubscribed from \"" + name + "\""
                         : "Failed: " + (err.empty() ? "unknown error" : err));
            if (ok) invalidatePodcastsCategory();
        });
    }).detach();
}

void BrowserWindow::OnRefresh(UINT, int, HWND) {
    m_search.SetWindowText(L"");
    loadArtists();   // resets search state too
}

// Every keystroke just (re)arms the debounce timer -- the actual request
// goes out from OnTimer once typing pauses, so fast typing doesn't fire one
// request per character.
void BrowserWindow::OnSearchChanged(UINT, int, HWND) {
    SetTimer(kSearchDebounceTimer, kSearchDebounceMs, nullptr);
}

void BrowserWindow::OnTimer(UINT_PTR id) {
    if (id != kSearchDebounceTimer) return;
    KillTimer(kSearchDebounceTimer);

    wchar_t buf[256] = {};
    m_search.GetWindowText(buf, 256);
    std::string query = wToU8(buf);

    // Any dispatch -- including clearing the box -- invalidates whatever
    // search request is still in flight.
    ++m_searchGeneration;

    if (query.size() < 2) {
        if (m_isSearching) restoreBrowseTree();
        return;
    }

    setStatus("Searching\u2026");
    std::uint64_t generation = m_searchGeneration;
    std::thread([this, query, generation]() {
        std::string err;
        auto results = navidrome::SubsonicClientWin::get().search(query, err);
        auto* payload = new LoadedPayload{};
        payload->error      = err;
        payload->generation = generation;
        for (auto& s : results.songs) {
            // Plain song node: displayName is the track title that enqueue
            // writes to the playlist. The " — artist" suffix is label-only.
            auto n = navidrome::makeSongNode(s);
            payload->nodes.push_back(n);
        }
        syncBrowserNodesToPlaylists(payload->nodes);
        if (!PostMessage(WM_NAVIDROME_SEARCH, reinterpret_cast<WPARAM>(payload), 0))
            delete payload;   // window already gone
    }).detach();
}

// ---------------------------------------------------------------------------
// Enqueue to foobar2000 playlist (call from main thread)
// ---------------------------------------------------------------------------
// The whole metadb / hint / playlist_manager / playback block is shared with
// macOS — see navidrome::enqueueBrowserNodes in main.cpp. Only the radio
// stream-URL lookup is platform-local (m_radioStations cache).
void BrowserWindow::enqueueNodes(std::vector<std::shared_ptr<NavidromeNode>> songs,
                                 bool play, bool clearFirst) {
    std::string status;
    navidrome::enqueueBrowserNodes(
        songs, play, clearFirst,
        [this](const std::string& id) { return radioStationURL(id); },
        status);
    setStatus(status);
}

void BrowserWindow::setStatus(const std::string& msg) {
    m_status.SetWindowText(u8ToWide(msg).c_str());
}
