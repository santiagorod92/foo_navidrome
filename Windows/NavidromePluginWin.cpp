#include "stdafx.h"
#include "BrowserWindow.h"
#include "SubsonicClientWin.h"
#include <SDK/cfg_var.h>
#include <SDK/album_art.h>
#include <SDK/album_art_helpers.h>
#include <string>
#include <cctype>
#pragma comment(lib, "winhttp.lib")

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
// GUIDs — must match NavidromePlugin.mm so settings persist cross-platform
// ---------------------------------------------------------------------------
static constexpr GUID guid_cfg_server_url = { 0xa1b2c3d4,0x1111,0x2222,{0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x01,0x01} };
static constexpr GUID guid_cfg_username   = { 0xa1b2c3d4,0x1111,0x2222,{0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x01,0x02} };
static constexpr GUID guid_cfg_password   = { 0xa1b2c3d4,0x1111,0x2222,{0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x01,0x03} };
static constexpr GUID guid_cfg_salt       = { 0xa1b2c3d4,0x1111,0x2222,{0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x01,0x04} };
static constexpr GUID guid_prefs_page     = { 0xa1b2c3d4,0x1111,0x2222,{0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x01,0x05} };
static constexpr GUID guid_mainmenu_group = { 0xa1b2c3d4,0x1111,0x2222,{0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x01,0x06} };
static constexpr GUID guid_mainmenu_cmd   = { 0xa1b2c3d4,0x1111,0x2222,{0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x01,0x07} };
static constexpr GUID guid_cfg_custom_headers = { 0xa1b2c3d4,0x1111,0x2222,{0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x01,0x0a} };

// ---------------------------------------------------------------------------
// Config vars
// ---------------------------------------------------------------------------
namespace navidrome {
    cfg_string cfg_server_url(guid_cfg_server_url, "http://localhost:4533/");
    cfg_string cfg_username  (guid_cfg_username,   "");
    cfg_string cfg_password  (guid_cfg_password,   "");
    cfg_string cfg_salt      (guid_cfg_salt,        "fb2k_navidrome");
    // Extra HTTP headers (one "Name: Value" per line) sent on every request —
    // API, cover art and audio stream. Used e.g. for Cloudflare Access
    // service-token headers when Navidrome sits behind a Zero Trust tunnel.
    cfg_string cfg_custom_headers(guid_cfg_custom_headers, "");
}

// ---------------------------------------------------------------------------
// Custom HTTP headers editor — a standalone window opened from the prefs page.
// Multiline "Name: Value" per line; persisted to cfg_custom_headers. The
// "Cloudflare headers" button inserts the two CF Access service-token header
// names so the user only has to paste the id/secret values.
// ---------------------------------------------------------------------------
class NavidromeHeadersWindow : public CWindowImpl<NavidromeHeadersWindow> {
public:
    DECLARE_WND_CLASS(L"foo_navidrome_HeadersWnd")

    static NavidromeHeadersWindow& get() { static NavidromeHeadersWindow inst; return inst; }

    void show() {
        if (!IsWindow()) {
            Create(nullptr, CWindow::rcDefault, L"Navidrome — Custom HTTP Headers",
                   WS_OVERLAPPEDWINDOW, 0);
            SetWindowPos(nullptr, 0, 0, 520, 360,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_SHOWWINDOW);
        } else {
            ShowWindow(SW_SHOW);
            SetForegroundWindow(*this);
        }
        loadText();
    }

    BEGIN_MSG_MAP(NavidromeHeadersWindow)
        MSG_WM_CREATE(OnCreate)
        MSG_WM_SIZE(OnSize)
        COMMAND_ID_HANDLER_EX(IDC_CF,     OnCloudflare)
        COMMAND_ID_HANDLER_EX(IDC_SAVE,   OnSave)
        COMMAND_ID_HANDLER_EX(IDC_CANCEL, OnCancel)
    END_MSG_MAP()

private:
    enum { IDC_EDIT = 3001, IDC_CF = 3002, IDC_SAVE = 3003, IDC_CANCEL = 3004, IDC_HINT = 3005 };
    CEdit m_edit;

    LRESULT OnCreate(LPCREATESTRUCT) {
        HFONT f = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        auto setFont = [&](HWND h) { SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(f), 0); };

        HWND hint = CreateWindowW(L"STATIC",
            L"One header per line, as  Name: Value  (e.g. for a Cloudflare Zero Trust tunnel).",
            WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, *this,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_HINT)), nullptr, nullptr);
        setFont(hint);

        m_edit.Create(*this, CWindow::rcDefault, nullptr,
            WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL |
            ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN, 0, IDC_EDIT);
        m_edit.SetFont(f);

        auto mkBtn = [&](int id, const wchar_t* label) {
            HWND b = CreateWindowW(L"BUTTON", label, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                0, 0, 10, 10, *this,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
            setFont(b);
        };
        mkBtn(IDC_CF,     L"Cloudflare headers");
        mkBtn(IDC_SAVE,   L"Save");
        mkBtn(IDC_CANCEL, L"Cancel");
        return 0;
    }

    LRESULT OnSize(UINT, CSize sz) {
        const int pad = 10, btnH = 26, btnW = 130, hintH = 18;
        int w = sz.cx, h = sz.cy;
        ::SetWindowPos(GetDlgItem(IDC_HINT), nullptr, pad, pad, w - 2 * pad, hintH,
                       SWP_NOZORDER);
        m_edit.SetWindowPos(nullptr, pad, pad + hintH + 4, w - 2 * pad,
                            h - hintH - btnH - 3 * pad - 4, SWP_NOZORDER);
        int by = h - btnH - pad;
        ::SetWindowPos(GetDlgItem(IDC_CF),     nullptr, pad, by, btnW, btnH, SWP_NOZORDER);
        ::SetWindowPos(GetDlgItem(IDC_CANCEL), nullptr, w - pad - 80, by, 80, btnH, SWP_NOZORDER);
        ::SetWindowPos(GetDlgItem(IDC_SAVE),   nullptr, w - 2 * pad - 80 - 80, by, 80, btnH, SWP_NOZORDER);
        return 0;
    }

    void loadText() {
        std::wstring w = u8ToWide(navidrome::cfg_custom_headers.get().c_str());
        m_edit.SetWindowText(w.c_str());
    }

    std::string editTextU8() {
        int len = m_edit.GetWindowTextLength();
        std::wstring w(len + 1, L'\0');
        m_edit.GetWindowText(&w[0], len + 1);
        w.resize(len);
        return wToU8(w);
    }

    void OnSave(UINT, int, HWND) {
        navidrome::cfg_custom_headers.set(editTextU8().c_str());
        ShowWindow(SW_HIDE);
    }

    void OnCancel(UINT, int, HWND) { ShowWindow(SW_HIDE); }

    // Append the two CF Access header names if they're not already present, so
    // the user just pastes the id/secret values after the colon.
    void OnCloudflare(UINT, int, HWND) {
        std::string text = editTextU8();
        std::string lower = text;
        for (char& c : lower) c = (char)tolower((unsigned char)c);
        auto ensure = [&](const char* headerName) {
            std::string needle = headerName;
            for (char& c : needle) c = (char)tolower((unsigned char)c);
            if (lower.find(needle) != std::string::npos) return;
            if (!text.empty() && text.back() != '\n') text += "\r\n";
            text += headerName;
            text += ": ";
            text += "\r\n";
            lower += needle;  // keep dedupe state consistent across both inserts
        };
        ensure("CF-Access-Client-Id");
        ensure("CF-Access-Client-Secret");
        m_edit.SetWindowText(u8ToWide(text).c_str());
        m_edit.SetFocus();
    }
};

// ---------------------------------------------------------------------------
// Preferences page (programmatic window — no .rc file required)
// ---------------------------------------------------------------------------
class NavidromePrefsInstance : public CWindowImpl<NavidromePrefsInstance>,
                               public preferences_page_instance {
public:
    DECLARE_WND_CLASS(L"foo_navidrome_PrefsWnd")

    explicit NavidromePrefsInstance(preferences_page_callback::ptr cb) : m_cb(cb) {}

    // preferences_page_instance
    HWND      get_wnd() override { return m_hWnd; }
    t_uint32  get_state() override {
        // preferences_state has no "unchanged" constant — the unchanged state is 0.
        return m_changed ? preferences_state::changed | preferences_state::resettable
                         : 0;
    }
    void apply()  override { saveSettings(); m_changed = false; notifyCb(); }
    void reset()  override {
        SetDlgItemText(IDC_URL,  L"http://localhost:4533/");
        SetDlgItemText(IDC_USER, L"");
        SetDlgItemText(IDC_PASS, L"");
        m_changed = true; notifyCb();
    }

    BEGIN_MSG_MAP(NavidromePrefsInstance)
        MSG_WM_CREATE(OnCreate)
        MESSAGE_HANDLER_EX(WM_TEST_RESULT, OnTestResult)
        COMMAND_HANDLER_EX(IDC_URL,  EN_CHANGE, OnChanged)
        COMMAND_HANDLER_EX(IDC_USER, EN_CHANGE, OnChanged)
        COMMAND_HANDLER_EX(IDC_PASS, EN_CHANGE, OnChanged)
        COMMAND_HANDLER_EX(IDC_TEST, BN_CLICKED, OnTest)
        COMMAND_HANDLER_EX(IDC_HEADERS, BN_CLICKED, OnHeaders)
    END_MSG_MAP()

private:
    enum { IDC_URL=1001, IDC_USER=1002, IDC_PASS=1003, IDC_TEST=1004, IDC_STATUS=1005, IDC_HEADERS=1006 };
    // Posted from the background ping thread back to the UI thread (see OnTest).
    static constexpr UINT WM_TEST_RESULT = WM_USER + 200;

    LRESULT OnCreate(LPCREATESTRUCT) {
        HFONT f = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        auto lbl = [&](const wchar_t* t, int x, int y, int w, int h) {
            HWND h2 = CreateWindowW(L"STATIC", t, WS_CHILD|WS_VISIBLE, x,y,w,h, *this, nullptr, nullptr, nullptr);
            SendMessageW(h2, WM_SETFONT, reinterpret_cast<WPARAM>(f), 0);
        };
        auto edit = [&](int id, int x, int y, int w, int h, bool pass=false) {
            DWORD sty = WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL|(pass?ES_PASSWORD:0);
            HWND h2 = CreateWindowW(L"EDIT", L"", sty, x,y,w,h, *this, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
            SendMessageW(h2, WM_SETFONT, reinterpret_cast<WPARAM>(f), 0);
        };
        lbl(L"Server URL:",  8, 14, 80, 18);  edit(IDC_URL,  92, 10, 290, 22);
        lbl(L"Username:",    8, 44, 80, 18);  edit(IDC_USER, 92, 40, 290, 22);
        lbl(L"Password:",    8, 74, 80, 18);  edit(IDC_PASS, 92, 70, 290, 22, true);

        HWND btn = CreateWindowW(L"BUTTON", L"Test Connection",
            WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 92,100, 110,24, *this,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_TEST)), nullptr, nullptr);
        SendMessageW(btn, WM_SETFONT, reinterpret_cast<WPARAM>(f), 0);

        HWND st = CreateWindowW(L"STATIC", L"", WS_CHILD|WS_VISIBLE|SS_LEFT, 210,105, 170,18, *this,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_STATUS)), nullptr, nullptr);
        SendMessageW(st, WM_SETFONT, reinterpret_cast<WPARAM>(f), 0);

        HWND hdr = CreateWindowW(L"BUTTON", L"Custom Headers…",
            WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 92,134, 130,24, *this,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_HEADERS)), nullptr, nullptr);
        SendMessageW(hdr, WM_SETFONT, reinterpret_cast<WPARAM>(f), 0);

        loadSettings();
        return 0;
    }

    void OnHeaders(UINT, int, HWND) { NavidromeHeadersWindow::get().show(); }

    void loadSettings() {
        SetDlgItemText(IDC_URL,  pfc::stringcvt::string_wide_from_utf8(navidrome::cfg_server_url.get().c_str()));
        SetDlgItemText(IDC_USER, pfc::stringcvt::string_wide_from_utf8(navidrome::cfg_username.get().c_str()));
        SetDlgItemText(IDC_PASS, pfc::stringcvt::string_wide_from_utf8(navidrome::cfg_password.get().c_str()));
    }

    void saveSettings() {
        auto getText = [&](int id) -> std::string {
            wchar_t buf[1024] = {};
            GetDlgItemText(id, buf, 1024);
            return pfc::stringcvt::string_utf8_from_wide(buf).get_ptr();
        };
        navidrome::cfg_server_url.set(getText(IDC_URL).c_str());
        navidrome::cfg_username.set(getText(IDC_USER).c_str());
        navidrome::cfg_password.set(getText(IDC_PASS).c_str());
    }

    void OnChanged(UINT, int, HWND) { m_changed = true; notifyCb(); }
    void notifyCb() { if (m_cb.is_valid()) m_cb->on_state_changed(); }

    void OnTest(UINT, int, HWND) {
        saveSettings();
        SetDlgItemText(IDC_STATUS, L"Testing\u2026");
        std::thread([this]() {
            std::string err;
            bool ok = navidrome::SubsonicClientWin::get().ping(err);
            PostMessage(WM_TEST_RESULT, ok ? 1 : 0,
                reinterpret_cast<LPARAM>(ok ? nullptr : new std::string(err)));
        }).detach();
    }

    // Runs on the UI thread; lParam owns a heap std::string with the error text
    // (null on success). Registered via MESSAGE_HANDLER_EX in the message map.
    LRESULT OnTestResult(UINT, WPARAM wParam, LPARAM lParam) {
        bool ok = wParam != 0;
        auto* errStr = reinterpret_cast<std::string*>(lParam);
        SetDlgItemText(IDC_STATUS, ok ? L"Connected!" :
            pfc::stringcvt::string_wide_from_utf8(errStr ? errStr->c_str() : "Failed"));
        delete errStr;
        return 0;
    }

    preferences_page_callback::ptr m_cb;
    bool m_changed = false;
};

class NavidromePrefsPageFactory : public preferences_page_v3 {
public:
    preferences_page_instance::ptr instantiate(HWND parent,
        preferences_page_callback::ptr cb) override {
        auto inst = fb2k::service_new<NavidromePrefsInstance>(cb);
        inst->Create(parent);
        return inst;
    }
    const char* get_name() override { return "Navidrome"; }
    GUID        get_guid() override { return guid_prefs_page; }
    GUID        get_parent_guid() override { return preferences_page::guid_tools; }
};
FB2K_SERVICE_FACTORY(NavidromePrefsPageFactory);

// ---------------------------------------------------------------------------
// Media Library preferences sub-page — makes "Navidrome" appear under
// Preferences > Media Library (parity with the macOS build, which parents a
// page to guid_media_library). The macOS page embeds the browser directly;
// on Windows the browser is a standalone top-level window, so this page just
// hosts an "Open Navidrome Browser" button that surfaces it.
// ---------------------------------------------------------------------------
class NavidromeLibraryPrefsInstance : public CWindowImpl<NavidromeLibraryPrefsInstance>,
                                      public preferences_page_instance {
public:
    DECLARE_WND_CLASS(L"foo_navidrome_LibPrefsWnd")

    explicit NavidromeLibraryPrefsInstance(preferences_page_callback::ptr cb) : m_cb(cb) {}

    // Nothing editable on this page — it's a launcher, so it's never "changed".
    HWND      get_wnd() override { return m_hWnd; }
    t_uint32  get_state() override { return 0; }
    void      apply() override {}
    void      reset() override {}

    BEGIN_MSG_MAP(NavidromeLibraryPrefsInstance)
        MSG_WM_CREATE(OnCreate)
        MSG_WM_SIZE(OnSize)
    END_MSG_MAP()

private:
    // Embed the browser inline, filling the page — parity with the macOS build
    // (which mounts the browser view controller directly in this sub-page). A
    // fresh BrowserWindow instance owned by this page, distinct from the
    // standalone-window singleton used by the File menu / library_viewer.
    LRESULT OnCreate(LPCREATESTRUCT) {
        m_browser.createEmbedded(*this);
        return 0;
    }

    void OnSize(UINT, CSize sz) {
        if (m_browser.IsWindow())
            m_browser.SetWindowPos(nullptr, 0, 0, sz.cx, sz.cy, SWP_NOZORDER);
    }

    BrowserWindow                  m_browser;
    preferences_page_callback::ptr m_cb;
};

class NavidromeLibraryPrefsFactory : public preferences_page_v3 {
public:
    preferences_page_instance::ptr instantiate(HWND parent,
        preferences_page_callback::ptr cb) override {
        auto inst = fb2k::service_new<NavidromeLibraryPrefsInstance>(cb);
        inst->Create(parent);
        return inst;
    }
    const char* get_name() override { return "Navidrome"; }
    // Match macOS guid_library_prefs (…01,0x09) for cross-platform tidiness.
    GUID        get_guid() override {
        return { 0xa1b2c3d4,0x1111,0x2222,{0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x01,0x09} };
    }
    GUID        get_parent_guid() override { return preferences_page::guid_media_library; }
};
FB2K_SERVICE_FACTORY(NavidromeLibraryPrefsFactory);

// ---------------------------------------------------------------------------
// Main menu: File > Open Navidrome Browser
// ---------------------------------------------------------------------------
class NavidromeMenuCmd : public mainmenu_commands {
public:
    t_uint32 get_command_count() override { return 1; }
    GUID     get_command(t_uint32 i) override {
        if (i == 0) return guid_mainmenu_cmd;
        throw pfc::exception_invalid_params();
    }
    void get_name(t_uint32 i, pfc::string_base& out) override {
        if (i == 0) { out = "Open Navidrome Browser"; return; }
        throw pfc::exception_invalid_params();
    }
    bool get_description(t_uint32 i, pfc::string_base& out) override {
        if (i == 0) { out = "Browse and stream from Navidrome"; return true; }
        return false;
    }
    GUID     get_parent() override { return mainmenu_groups::file; }
    t_uint32 get_sort_priority() override { return 0xFF; }
    bool     get_display(t_uint32 i, pfc::string_base& out, t_uint32& flags) override {
        get_name(i, out); flags = 0; return true;
    }
    void execute(t_uint32 i, service_ptr_t<service_base>) override {
        if (i != 0) throw pfc::exception_invalid_params();
        fb2k::inMainThread([] { BrowserWindow::get().show(); });
    }
};
FB2K_SERVICE_FACTORY(NavidromeMenuCmd);

// ---------------------------------------------------------------------------
// Album art fallback — serves cover art from Navidrome's getCoverArt endpoint
// ---------------------------------------------------------------------------
static std::string urlParam(const char* url, const char* key) {
    std::string k = std::string(key) + "=";
    const char* p = strstr(url, k.c_str());
    if (!p) return "";
    p += k.size();
    const char* e = strchr(p, '&');
    return e ? std::string(p, e) : std::string(p);
}

class NavidromeArtInstance : public album_art_extractor_instance_v2 {
public:
    explicit NavidromeArtInstance(const char* songId) : m_id(songId) {}

    album_art_data_ptr query(const GUID& what, abort_callback&) override {
        if (what != album_art_ids::cover_front) throw exception_album_art_not_found();

        std::string url = navidrome::SubsonicClientWin::get().coverArtURL(m_id, 0);
        // Fetch binary image data with WinHTTP (SubsonicClientWin::httpGet is private —
        // duplicated here rather than exposed, matching the original fallback's approach).
        std::string body;
        HINTERNET hSess = WinHttpOpen(L"foo_navidrome/1.0",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (hSess) {
            // Offer modern TLS only (TLS 1.2/1.3). See applySecureProtocols in
            // SubsonicClientWin.cpp for the Wine min-TLS-1.3 caveat.
            {
                DWORD proto = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
                proto |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
                WinHttpSetOption(hSess, WINHTTP_OPTION_SECURE_PROTOCOLS, &proto, sizeof(proto));
            }
            std::wstring wurl(url.begin(), url.end());
            // Proper wide conversion
            int n = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, nullptr, 0);
            wurl.resize(n); MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, &wurl[0], n);
            if (!wurl.empty() && wurl.back()==0) wurl.pop_back();

            URL_COMPONENTS uc = {}; uc.dwStructSize = sizeof(uc);
            wchar_t host[256]={}, path[4096]={};
            uc.lpszHostName=host; uc.dwHostNameLength=256;
            uc.lpszUrlPath=path;  uc.dwUrlPathLength=4096;
            if (WinHttpCrackUrl(wurl.c_str(),0,0,&uc)) {
                HINTERNET hConn = WinHttpConnect(hSess, host, uc.nPort, 0);
                if (hConn) {
                    DWORD flags = (uc.nScheme==INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
                    HINTERNET hReq = WinHttpOpenRequest(hConn, L"GET", path, nullptr,
                        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
                    if (hReq) {
                        std::wstring hdrs = navidrome::SubsonicClientWin::customHeadersWide();
                        if (!hdrs.empty())
                            WinHttpAddRequestHeaders(hReq, hdrs.c_str(), (DWORD)-1,
                                WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
                        if (WinHttpSendRequest(hReq,nullptr,0,nullptr,0,0,0) &&
                            WinHttpReceiveResponse(hReq,nullptr)) {
                            DWORD status=0,sz=sizeof(status);
                            WinHttpQueryHeaders(hReq,
                                WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,
                                nullptr,&status,&sz,nullptr);
                            if (status==200) {
                                DWORD avail=0;
                                while (WinHttpQueryDataAvailable(hReq,&avail) && avail>0) {
                                    std::string chunk(avail,'\0');
                                    DWORD read=0;
                                    WinHttpReadData(hReq,&chunk[0],avail,&read);
                                    body.append(chunk,0,read);
                                }
                            }
                        }
                        WinHttpCloseHandle(hReq);
                    }
                    WinHttpCloseHandle(hConn);
                }
            }
            WinHttpCloseHandle(hSess);
        }
        if (body.empty()) throw exception_album_art_not_found();
        return album_art_data_impl::g_create(body.data(), body.size());
    }

    album_art_path_list::ptr query_paths(const GUID&, abort_callback&) override {
        throw exception_album_art_not_found();
    }

private:
    std::string m_id;
};

// Prefer the album/folder coverArt id embedded at enqueue time; fall back to the song id
// (query "id=" for legacy URLs, or the <id> path segment of navidrome://track/<id>).
static std::string resolveArtId(const char* path) {
    std::string id = urlParam(path, "coverArt");
    if (id.empty()) id = urlParam(path, "id");
    if (id.empty() && strncmp(path, "navidrome://track/", 18) == 0) {
        const char* idStart = path + 18;
        const char* idEnd = strchr(idStart, '?');
        if (!idEnd) idEnd = idStart + strlen(idStart);
        if (idEnd > idStart) id.assign(idStart, idEnd - idStart);
    }
    return id;
}

// Registered as a full album_art_extractor (NOT album_art_fallback, which this used to be):
// album_art_fallback only runs after every other registered source has already declined, and
// in practice album_art_manager_v2's cold per-file query throws "Attached picture not found"
// before fallback gets a chance — matches the reasoning already documented on the macOS side
// (NavidromeArtExtractor.mm) for using a real extractor instead. Returning true from
// is_our_path() guarantees foobar calls open() directly.
class NavidromeArtExtractorWin : public album_art_extractor {
public:
    bool is_our_path(const char* path, const char* /*ext*/) override {
        if (!path) return false;
        if (strncmp(path, "navidrome://", 12) == 0) return true;
        return strstr(path, "/rest/stream.view") != nullptr;
    }

    album_art_extractor_instance_ptr open(file_ptr /*filehint*/, const char* path,
                                          abort_callback&) override {
        std::string id = resolveArtId(path);
        if (id.empty()) throw exception_album_art_not_found();
        return fb2k::service_new<NavidromeArtInstance>(id.c_str());
    }
};
FB2K_SERVICE_FACTORY(NavidromeArtExtractorWin);
