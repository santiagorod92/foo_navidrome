#include "stdafx.h"
#include "BrowserWindow.h"
#include "SubsonicClientWin.h"
#include "MediaEnrichmentLogic.h"
#include "../NavidromePlaylistSync.h"
#include "EsLyricBridge.h"
#include <SDK/cfg_var.h>
#include <SDK/album_art.h>
#include <SDK/album_art_helpers.h>
#include <SDK/initquit.h>
#include <SDK/play_callback.h>
#include <algorithm>
#include <string>
#include <cctype>
#include <mutex>
#include <set>
#include <thread>
#pragma comment(lib, "winhttp.lib")

namespace {
    void refreshEsLyricBridge() {
        auto ctx = navidrome::SubsonicClientWin::get().snapshot();
        std::string err = navidrome::EsLyricBridge::installOrUpdate(ctx);
        if (!err.empty())
            console::print(("ESLyric bridge error: " + err).c_str());
    }
}

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
static constexpr GUID guid_mainmenu_bookmark_cmd =
    { 0xa1b2c3d4,0x1111,0x2222,{0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x01,0x0e} };
static constexpr GUID guid_cfg_custom_headers = { 0xa1b2c3d4,0x1111,0x2222,{0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x01,0x0a} };
static constexpr GUID guid_cfg_scrobble   = { 0xa1b2c3d4,0x1111,0x2222,{0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x01,0x0b} };
static constexpr GUID guid_cfg_stream_format = { 0xa1b2c3d4,0x1111,0x2222,{0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x01,0x0c} };
static constexpr GUID guid_cfg_max_bitrate   = { 0xa1b2c3d4,0x1111,0x2222,{0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x01,0x0d} };

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
    // Report plays back to Navidrome (play counts, "Recently Played", and any
    // Last.fm / ListenBrainz relay the server has configured).
    // Qualified: an unqualified cfg_bool resolves to the legacy
    // cfg_int_t<bool> (no set()) here, and the two flavours serialize
    // differently — both platforms must use the same one.
    cfg_var_modern::cfg_bool cfg_scrobble(guid_cfg_scrobble, true);

    // Transcoding preferences, applied to every stream.view request.
    // cfg_stream_format: "" = let the server decide, "raw" = never transcode,
    // otherwise a Subsonic format name ("mp3", "opus", "aac", …).
    // cfg_max_bitrate: kbps ceiling; 0 = unlimited.
    // Qualified for the same reason as cfg_scrobble — an unqualified cfg_int
    // resolves to the legacy cfg_int_t<t_int32>, which has no set() and
    // serializes differently.
    cfg_string cfg_stream_format(guid_cfg_stream_format, "");
    cfg_var_modern::cfg_int cfg_max_bitrate(guid_cfg_max_bitrate, 0);
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
        navidrome::CoverCache::instance().clear();
        refreshEsLyricBridge();
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
    void apply()  override {
        saveSettings();
        navidrome::CoverCache::instance().clear();
        refreshEsLyricBridge();
        m_changed = false;
        notifyCb();
    }
    void reset()  override {
        SetDlgItemText(IDC_URL,  L"http://localhost:4533/");
        SetDlgItemText(IDC_USER, L"");
        SetDlgItemText(IDC_PASS, L"");
        CheckDlgButton(IDC_SCROBBLE, BST_CHECKED);
        m_format.SetCurSel(0);
        m_bitrate.SetCurSel(0);
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
        COMMAND_HANDLER_EX(IDC_SCROBBLE, BN_CLICKED, OnChanged)
        COMMAND_HANDLER_EX(IDC_FORMAT,  CBN_SELCHANGE, OnChanged)
        COMMAND_HANDLER_EX(IDC_BITRATE, CBN_SELCHANGE, OnChanged)
    END_MSG_MAP()

private:
    enum { IDC_URL=1001, IDC_USER=1002, IDC_PASS=1003, IDC_TEST=1004, IDC_STATUS=1005,
           IDC_HEADERS=1006, IDC_SCROBBLE=1007, IDC_FORMAT=1008, IDC_BITRATE=1009 };

    // Streaming transcode options. The stored value is what goes on the wire as
    // stream.view's `format` — "" leaves the decision to the server's own
    // transcoding rules, "raw" forces the original file.
    //
    // The server can only honour a format it has a transcoding configured for.
    // Navidrome ships mp3 / opus / aac; FLAC and WAV need a transcoding row
    // added in its admin UI first, and are mainly useful as lossless
    // normalisation targets for source codecs foobar2000 can't decode itself.
    struct FormatOption { const wchar_t* label; const char* value; };
    static const FormatOption* formatOptions(std::size_t& count) {
        static const FormatOption kFormats[] = {
            { L"Server default",            ""     },
            { L"Original (no transcoding)", "raw"  },
            { L"MP3",                       "mp3"  },
            { L"Opus",                      "opus" },
            { L"AAC",                       "aac"  },
            { L"FLAC (lossless)",           "flac" },
            { L"WAV (uncompressed)",        "wav"  },
        };
        count = sizeof(kFormats) / sizeof(kFormats[0]);
        return kFormats;
    }
    // kbps ceiling; 0 means "no limit", which is also what Subsonic reads when
    // the parameter is absent.
    static const int* bitrateOptions(std::size_t& count) {
        static const int kBitrates[] = { 0, 64, 96, 128, 192, 256, 320 };
        count = sizeof(kBitrates) / sizeof(kBitrates[0]);
        return kBitrates;
    }
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

        HWND scr = CreateWindowW(L"BUTTON", L"Report plays to Navidrome (scrobbling)",
            WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX, 92,166, 290,20, *this,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SCROBBLE)), nullptr, nullptr);
        SendMessageW(scr, WM_SETFONT, reinterpret_cast<WPARAM>(f), 0);

        // Streaming transcode controls. Both are per-request stream.view params,
        // so a change takes effect on the next track without reconnecting.
        lbl(L"Stream as:",   8, 198, 80, 18);
        lbl(L"Max bitrate:", 8, 228, 80, 18);

        // CBS_DROPDOWNLIST height is the *dropped* height, not the closed one.
        m_format.Create(*this, CWindow::rcDefault, nullptr,
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_VSCROLL|CBS_DROPDOWNLIST, 0, IDC_FORMAT);
        m_format.SetWindowPos(nullptr, 92, 194, 180, 200, SWP_NOZORDER);
        m_format.SetFont(f);
        std::size_t formatCount = 0;
        const FormatOption* formats = formatOptions(formatCount);
        for (std::size_t i = 0; i < formatCount; ++i) m_format.AddString(formats[i].label);

        m_bitrate.Create(*this, CWindow::rcDefault, nullptr,
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_VSCROLL|CBS_DROPDOWNLIST, 0, IDC_BITRATE);
        m_bitrate.SetWindowPos(nullptr, 92, 224, 180, 200, SWP_NOZORDER);
        m_bitrate.SetFont(f);
        std::size_t bitrateCount = 0;
        const int* bitrates = bitrateOptions(bitrateCount);
        for (std::size_t i = 0; i < bitrateCount; ++i) {
            m_bitrate.AddString(bitrates[i] == 0
                ? L"Unlimited"
                : (std::to_wstring(bitrates[i]) + L" kbps").c_str());
        }

        loadSettings();
        return 0;
    }

    void OnHeaders(UINT, int, HWND) { NavidromeHeadersWindow::get().show(); }

    void loadSettings() {
        SetDlgItemText(IDC_URL,  pfc::stringcvt::string_wide_from_utf8(navidrome::cfg_server_url.get().c_str()));
        SetDlgItemText(IDC_USER, pfc::stringcvt::string_wide_from_utf8(navidrome::cfg_username.get().c_str()));
        SetDlgItemText(IDC_PASS, pfc::stringcvt::string_wide_from_utf8(navidrome::cfg_password.get().c_str()));
        CheckDlgButton(IDC_SCROBBLE, navidrome::cfg_scrobble.get() ? BST_CHECKED : BST_UNCHECKED);

        std::string format = navidrome::cfg_stream_format.get().c_str();
        std::size_t formatCount = 0;
        const FormatOption* formats = formatOptions(formatCount);
        int formatIndex = 0;
        for (std::size_t i = 0; i < formatCount; ++i)
            if (format == formats[i].value) { formatIndex = static_cast<int>(i); break; }
        m_format.SetCurSel(formatIndex);

        int bitrate = static_cast<int>(navidrome::cfg_max_bitrate.get());
        std::size_t bitrateCount = 0;
        const int* bitrates = bitrateOptions(bitrateCount);
        int bitrateIndex = 0;
        for (std::size_t i = 0; i < bitrateCount; ++i)
            if (bitrates[i] == bitrate) { bitrateIndex = static_cast<int>(i); break; }
        m_bitrate.SetCurSel(bitrateIndex);
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
        navidrome::cfg_scrobble.set(IsDlgButtonChecked(IDC_SCROBBLE) == BST_CHECKED);

        std::size_t formatCount = 0;
        const FormatOption* formats = formatOptions(formatCount);
        int fi = m_format.GetCurSel();
        if (fi >= 0 && static_cast<std::size_t>(fi) < formatCount)
            navidrome::cfg_stream_format.set(formats[fi].value);

        std::size_t bitrateCount = 0;
        const int* bitrates = bitrateOptions(bitrateCount);
        int bi = m_bitrate.GetCurSel();
        if (bi >= 0 && static_cast<std::size_t>(bi) < bitrateCount)
            navidrome::cfg_max_bitrate.set(bitrates[bi]);
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

    CComboBox m_format, m_bitrate;
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
    t_uint32 get_command_count() override { return 2; }
    GUID     get_command(t_uint32 i) override {
        if (i == 0) return guid_mainmenu_cmd;
        if (i == 1) return guid_mainmenu_bookmark_cmd;
        throw pfc::exception_invalid_params();
    }
    void get_name(t_uint32 i, pfc::string_base& out) override {
        if (i == 0) { out = "Open Navidrome Browser"; return; }
        if (i == 1) { out = "Bookmark Current Position"; return; }
        throw pfc::exception_invalid_params();
    }
    bool get_description(t_uint32 i, pfc::string_base& out) override {
        if (i == 0) { out = "Browse and stream from Navidrome"; return true; }
        if (i == 1) { out = "Save the current playback position to resume later"; return true; }
        return false;
    }
    GUID     get_parent() override { return mainmenu_groups::file; }
    t_uint32 get_sort_priority() override { return 0xFF; }
    bool     get_display(t_uint32 i, pfc::string_base& out, t_uint32& flags) override {
        get_name(i, out); flags = 0; return true;
    }
    void execute(t_uint32 i, service_ptr_t<service_base>) override {
        if (i == 0) { fb2k::inMainThread([] { BrowserWindow::get().show(); }); return; }
        if (i == 1) { bookmarkCurrentPosition(); return; }
        throw pfc::exception_invalid_params();
    }

private:
    // Saves the currently-playing track's position as a Navidrome bookmark.
    // createBookmark.view is an upsert, so this also updates any existing one.
    static void bookmarkCurrentPosition() {
        metadb_handle_ptr track;
        auto pc = playback_control::get();
        if (!pc->get_now_playing(track) || track.is_empty()) {
            console::print("Navidrome: no track is currently playing");
            return;
        }
        std::string songId = navidrome::trackIdFromURI(track->get_path());
        if (songId.empty()) {
            console::print("Navidrome: current track isn't from Navidrome");
            return;
        }
        double positionMs = pc->playback_get_position() * 1000.0;
        std::thread([songId, positionMs]() {
            std::string err;
            navidrome::SubsonicClientWin::get().createBookmark(songId, positionMs, "", err);
            if (!err.empty())
                console::printf("Navidrome: failed to save bookmark: %s", err.c_str());
        }).detach();
    }
};
FB2K_SERVICE_FACTORY(NavidromeMenuCmd);

// ---------------------------------------------------------------------------
// Cover art extractor — serves cover art from Navidrome's getCoverArt
// endpoint, matching both navidrome:// URIs and legacy /rest/stream.view
// URLs. A real extractor (not a fallback) so foobar always calls open() for
// our paths, and results are cached in-process (see MediaEnrichmentLogic.h)
// to avoid refetching the same cover for every track in an album.
// ---------------------------------------------------------------------------
namespace {
    // Session-deduped console diagnostics for non-not-found cover failures,
    // so a broken server doesn't spam the console once per track.
    std::mutex g_coverDiagMutex;
    std::set<std::pair<navidrome::FetchClass, std::string>> g_coverDiagSeen;

    void logCoverError(navidrome::FetchClass cls, const std::string& id) {
        using namespace navidrome;
        if (cls == FetchClass::NotFound) return; // not-found is silent (normal)

        {
            std::lock_guard<std::mutex> lock(g_coverDiagMutex);
            if (!g_coverDiagSeen.insert({cls, id}).second) return; // already logged
        }

        const char* msg = "";
        switch (cls) {
            case FetchClass::Auth:           msg = "Cover art fetch: authentication failed"; break;
            case FetchClass::ServerError:    msg = "Cover art fetch: server error"; break;
            case FetchClass::Transport:      msg = "Cover art fetch: network transport error"; break;
            case FetchClass::InvalidContent: msg = "Cover art fetch: invalid content"; break;
            default: break;
        }
        if (*msg) {
            console::print(msg);
        }
    }
}

class NavidromeArtInstance : public album_art_extractor_instance_v2 {
public:
    NavidromeArtInstance(const std::string& coverId,
                         const navidrome::SubsonicRequestContext& ctx)
        : m_id(coverId), m_context(ctx) {}

    album_art_data_ptr query(const GUID& what, abort_callback& abort) override {
        if (what != album_art_ids::cover_front) throw exception_album_art_not_found();

        // Check cache first
        auto cached = navidrome::CoverCache::instance().get(
            m_context.serverUrl, m_context.username, m_id);
        if (!cached.empty()) {
            return album_art_data_impl::g_create(cached.data(), cached.size());
        }

        // Fetch from server
        std::string url = navidrome::SubsonicClientWin::get().coverArtURL(
            m_context, m_id, 0);
        static constexpr std::size_t kMaxCoverBytes = 20 * 1024 * 1024; // 20 MB

        auto result = navidrome::SubsonicClientWin::get().httpGetBinary(
            m_context, url, kMaxCoverBytes, abort);

        if (result.cls == navidrome::FetchClass::Aborted) {
            throw exception_aborted();
        }

        if (result.cls != navidrome::FetchClass::Ok) {
            logCoverError(result.cls, m_id);
            throw exception_album_art_not_found();
        }

        // Cache success
        navidrome::CoverCache::instance().put(
            m_context.serverUrl, m_context.username, m_id, result.body);

        return album_art_data_impl::g_create(result.body.data(), result.body.size());
    }

    album_art_path_list::ptr query_paths(const GUID&, abort_callback&) override {
        throw exception_album_art_not_found();
    }

private:
    std::string m_id;
    navidrome::SubsonicRequestContext m_context;
};

class NavidromeArtExtractor : public album_art_extractor {
public:
    bool is_our_path(const char* p, const char*) override {
        if (!p) return false;
        // Match navidrome:// OR legacy /rest/stream.view
        return strncmp(p, "navidrome://", 12) == 0 || strstr(p, "/rest/stream.view") != nullptr;
    }

    album_art_extractor_instance_ptr open(file_ptr, const char* path,
                                          abort_callback&) override {
        std::string id = navidrome::resolveArtId(path);
        if (id.empty()) throw exception_album_art_not_found();

        auto ctx = navidrome::SubsonicClientWin::get().snapshot();
        return fb2k::service_new<NavidromeArtInstance>(id, ctx);
    }
};
FB2K_SERVICE_FACTORY(NavidromeArtExtractor);

// ---------------------------------------------------------------------------
// Scrobbler — reports plays back to Navidrome so play counts, "Recently
// Played" and any Last.fm / ListenBrainz relay configured server-side reflect
// what's played through foobar2000.
//
// Two calls per track, matching the Subsonic contract: submission=false on
// start ("now playing"), submission=true once enough of the track has been
// heard (half its length, capped at 4 minutes — the Last.fm convention).
// ---------------------------------------------------------------------------
class NavidromeScrobbler : public play_callback_static {
public:
    unsigned get_flags() override {
        return flag_on_playback_new_track | flag_on_playback_time |
               flag_on_playback_stop;
    }

    void on_playback_new_track(metadb_handle_ptr track) override {
        m_songId.clear();
        m_submitted = false;
        m_length    = 0.0;
        if (track.is_empty()) return;

        const std::string songId = navidrome::trackIdFromURI(track->get_path());
        if (songId.empty()) return;   // not one of ours

        // Deliberately ahead of the scrobble gate: this is a display refresh,
        // not a play report, so it must not follow the scrobbling preference.
        refreshRatingAsync(songId);

        if (!navidrome::cfg_scrobble.get()) return;
        m_songId = songId;
        m_length = track->get_length();
        scrobbleAsync(m_songId, false);
    }

    void on_playback_time(double time) override {
        if (m_songId.empty() || m_submitted) return;
        // Unknown length (live stream): fall back to the 4-minute cap alone.
        double threshold = (m_length > 0) ? (std::min)(240.0, m_length * 0.5) : 240.0;
        if (time < threshold) return;
        m_submitted = true;
        scrobbleAsync(m_songId, true);
    }

    void on_playback_stop(play_control::t_stop_reason) override {
        m_songId.clear();
        m_submitted = false;
    }

    // Unused callbacks (not requested in get_flags, but the interface is pure).
    void on_playback_starting(play_control::t_track_command, bool) override {}
    void on_playback_seek(double) override {}
    void on_playback_pause(bool) override {}
    void on_playback_edited(metadb_handle_ptr) override {}
    void on_playback_dynamic_info(const file_info&) override {}
    void on_playback_dynamic_info_track(const file_info&) override {}
    void on_volume_change(float) override {}

private:
    // Fire and forget on a worker thread — a slow or unreachable server must
    // never stall playback, and a failed scrobble isn't worth interrupting for.
    static void scrobbleAsync(std::string songId, bool submission) {
        std::thread([songId, submission]() {
            std::string err;
            navidrome::SubsonicClientWin::get().scrobble(songId, submission, err);
        }).detach();
    }

    // One extra request per played track. That's the only moment we can pick up
    // a rating changed outside foobar (the Navidrome web UI, another client)
    // without polling every playlist entry — Subsonic has no bulk rating
    // lookup, so a whole-playlist refresh would be one request per track.
    static void refreshRatingAsync(std::string songId) {
        std::thread([songId]() {
            std::string err;
            navidrome::Song song;
            if (!navidrome::SubsonicClientWin::get().getSong(songId, song, err)) return;
            navidrome::RatingUpdate u;
            u.songId  = songId;
            u.rating  = song.rating;
            u.starred = song.starred;
            std::vector<navidrome::RatingUpdate> updates;
            updates.push_back(std::move(u));
            navidrome::syncRatingsToPlaylists(std::move(updates));
        }).detach();
    }

    std::string m_songId;
    double      m_length    = 0.0;
    bool        m_submitted = false;
};
static play_callback_static_factory_t<NavidromeScrobbler> g_navidrome_scrobbler_factory;

// Startup refresh — mirrors navidromeRefreshRatingsOnStart() in
// NavidromePlugin.mm.

static void navidromeRefreshRatingsOnStart() {
    // Main thread: walking the playlists is a main-thread operation.
    navidrome::PlaylistAlbumScan scan = navidrome::scanPlaylistAlbums();


    // Nothing of ours in any playlist — the only exit that stays quiet. Every
    // other one says why, because "hook never fired" and "hook fired and found
    // nothing" are otherwise indistinguishable from the outside.
    if (scan.entries == 0) return;

    if (!navidrome::refreshRatingsOnStartEnabled()) return;
    if (!navidrome::SubsonicClientWin::get().isConfigured()) return;

    // Skipping coverage silently is how a partial refresh gets mistaken for a
    // complete one, so the two outcomes that leave entries behind say so.
    if (scan.albumIds.empty()) {
        std::string msg = "Navidrome: " + std::to_string(scan.entries) +
            " playlist entry/entries carry no album id (added by an older version)"
            " — open their album in the browser to refresh them";
        console::print(msg.c_str());
        return;
    }

    const std::size_t ungrouped = scan.ungrouped;
    std::thread([albumIds = std::move(scan.albumIds), ungrouped]() {
        std::vector<navidrome::RatingUpdate> updates;
        std::size_t failed = 0;
        for (const auto& albumId : albumIds) {
            std::string err;
            auto songs = navidrome::SubsonicClientWin::get().getSongsForAlbum(albumId, err);
            if (!err.empty()) { ++failed; continue; }
            for (const auto& song : songs) {
                if (song.id.empty()) continue;
                navidrome::RatingUpdate u;
                u.songId  = song.id;
                u.rating  = song.rating;
                u.starred = song.starred;
                updates.push_back(std::move(u));
            }
        }
        // One sync for everything: it walks every playlist once, so doing it per
        // album would repeat that walk for no gain.
        const std::size_t ok = albumIds.size() - failed;
        navidrome::syncRatingsToPlaylists(std::move(updates));

        std::string msg = "Navidrome: refreshed ratings from " + std::to_string(ok) + " album(s)";
        if (failed > 0)    msg += ", " + std::to_string(failed) + " album(s) failed";
        if (ungrouped > 0) msg += ", " + std::to_string(ungrouped) +
            " entry/entries skipped (no album id, added by an older version)";
        console::print(msg.c_str());
    }).detach();
}

// initquit on both platforms — see NavidromePlugin.mm.
class NavidromeStartupRefresh : public initquit {
public:
    void on_init() override { navidromeRefreshRatingsOnStart(); }
};

static initquit_factory_t<NavidromeStartupRefresh> g_navidrome_startup_refresh_factory;

// ---------------------------------------------------------------------------
// Init/quit — installs/refreshes the ESLyric bridge on startup so lyrics work
// without opening prefs first; a no-op when ESLyric isn't installed.
// ---------------------------------------------------------------------------
class NavidromeInitQuit : public initquit {
public:
    void on_init() override {
        if (!navidrome::EsLyricBridge::isEsLyricInstalled()) {
            console::print("ESLyric not detected (playback unaffected)");
            return;
        }
        refreshEsLyricBridge();
    }

    void on_quit() override {}
};
FB2K_SERVICE_FACTORY(NavidromeInitQuit);
