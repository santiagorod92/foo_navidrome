#include "stdafx.h"
#include "BrowserWindow.h"
#include "SubsonicClientWin.h"
#include "../../core/MediaEnrichmentLogic.h"
#include "../../core/NavidromePlaylistSync.h"
#include "../../core/NavidromeDebugLog.h"
#include "../../core/NavidromeAudioMuse.h"
#include "../../core/NavidromeDiagnostics.h"
#include "EsLyricBridge.h"
#include "WinUi.h"
#include <SDK/cfg_var.h>
#include <SDK/album_art.h>
#include <SDK/album_art_helpers.h>
#include <SDK/initquit.h>
#include <SDK/play_callback.h>
#include <algorithm>
#include <memory>
#include <string>
#include <shellapi.h>
#include <cctype>
#include <chrono>
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

using navidrome::win::u8ToWide;
using navidrome::win::wToU8;

static constexpr GUID guid_cfg_server_url = { 0xa1b2c3d4,0x1111,0x2222,{0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x01,0x01} };
static constexpr GUID guid_cfg_username   = { 0xa1b2c3d4,0x1111,0x2222,{0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x01,0x02} };
static constexpr GUID guid_cfg_password   = { 0xa1b2c3d4,0x1111,0x2222,{0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x01,0x03} };
static constexpr GUID guid_cfg_salt       = { 0xa1b2c3d4,0x1111,0x2222,{0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x01,0x04} };
static constexpr GUID guid_prefs_page     = { 0xa1b2c3d4,0x1111,0x2222,{0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x01,0x05} };
static constexpr GUID guid_mainmenu_cmd   = { 0xa1b2c3d4,0x1111,0x2222,{0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x01,0x07} };
static constexpr GUID guid_mainmenu_bookmark_cmd =
    { 0xa1b2c3d4,0x1111,0x2222,{0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x01,0x0e} };
static constexpr GUID guid_cfg_custom_headers = { 0xa1b2c3d4,0x1111,0x2222,{0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x01,0x0a} };
static constexpr GUID guid_cfg_scrobble   = { 0xa1b2c3d4,0x1111,0x2222,{0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x01,0x0b} };
static constexpr GUID guid_cfg_stream_format = { 0xa1b2c3d4,0x1111,0x2222,{0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x01,0x0c} };
static constexpr GUID guid_cfg_max_bitrate   = { 0xa1b2c3d4,0x1111,0x2222,{0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x01,0x0d} };
static constexpr GUID guid_cfg_library_filter = { 0xa1b2c3d4,0x1111,0x2222,{0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x01,0x10} };
static constexpr GUID guid_cfg_library_ids   = { 0xa1b2c3d4,0x1111,0x2222,{0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x01,0x11} };
static constexpr GUID guid_cfg_browser_hidden_categories = { 0xa1b2c3d4,0x1111,0x2222,{0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x01,0x16} };

namespace navidrome {

    cfg_string cfg_server_url(guid_cfg_server_url, "http://localhost:4533/");
    cfg_string cfg_username  (guid_cfg_username,   "");
    cfg_string cfg_password  (guid_cfg_password,   "");
    cfg_string cfg_salt      (guid_cfg_salt,        "fb2k_navidrome");
    cfg_string cfg_custom_headers(guid_cfg_custom_headers, "");
    cfg_var_modern::cfg_bool cfg_scrobble(guid_cfg_scrobble, true);

    cfg_string cfg_stream_format(guid_cfg_stream_format, "");
    cfg_var_modern::cfg_int cfg_max_bitrate(guid_cfg_max_bitrate, 0);

    cfg_var_modern::cfg_bool cfg_library_filter(guid_cfg_library_filter, false);
    cfg_string cfg_library_ids(guid_cfg_library_ids, "");
    cfg_string cfg_browser_hidden_categories(guid_cfg_browser_hidden_categories, "");
}

class NavidromeHeadersWindow : public CWindowImpl<NavidromeHeadersWindow> {
public:
    DECLARE_WND_CLASS(L"foo_navidrome_HeadersWnd")

    static NavidromeHeadersWindow& get() { static NavidromeHeadersWindow inst; return inst; }

    void show() {
        if (!IsWindow()) {
            Create(nullptr, CWindow::rcDefault, L"Navidrome \u2014 Custom HTTP Headers",
                   WS_OVERLAPPEDWINDOW, 0);
            const navidrome::win::UiScale s(*this);
            SetWindowPos(nullptr, 0, 0, s(520), s(360),
                         SWP_NOMOVE | SWP_NOZORDER | SWP_SHOWWINDOW);
        } else {
            ShowWindow(SW_SHOW);
            SetForegroundWindow(*this);
        }
        loadText();
    }

    BEGIN_MSG_MAP(NavidromeHeadersWindow)
        MSG_WM_ERASEBKGND(OnEraseBkgnd)
        MSG_WM_CREATE(OnCreate)
        MSG_WM_SIZE(OnSize)
        COMMAND_ID_HANDLER_EX(IDC_CF,     OnCloudflare)
        COMMAND_ID_HANDLER_EX(IDC_SAVE,   OnSave)
        COMMAND_ID_HANDLER_EX(IDC_CANCEL, OnCancel)
    END_MSG_MAP()

    BOOL OnEraseBkgnd(CDCHandle dc) { navidrome::win::eraseLikeDialog(*this, dc); return TRUE; }

private:
    enum { IDC_EDIT = 3001, IDC_CF = 3002, IDC_SAVE = 3003, IDC_CANCEL = 3004, IDC_HINT = 3005 };
    CEdit m_edit;
    int   m_lineH = 16;
    fb2k::CCoreDarkModeHooks m_darkMode;

    LRESULT OnCreate(LPCREATESTRUCT) {
        HFONT f = navidrome::win::uiFont(*this);
        m_lineH = navidrome::win::lineHeight(*this, f);
        auto setFont = [&](HWND h) { navidrome::win::setFont(h, f); };

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
        m_darkMode.AddDialogWithControls(*this);
        return 0;
    }

    LRESULT OnSize(UINT, CSize sz) {
        using navidrome::win::fitWidth;
        const navidrome::win::UiScale s(*this);
        const int pad = s(10), btnH = (std::max)(s(26), m_lineH + s(10)), hintH = m_lineH;
        int w = sz.cx, h = sz.cy;
        ::SetWindowPos(GetDlgItem(IDC_HINT), nullptr, pad, pad, w - 2 * pad, hintH,
                       SWP_NOZORDER);
        m_edit.SetWindowPos(nullptr, pad, pad + hintH + s(4), w - 2 * pad,
                            h - hintH - btnH - 3 * pad - s(4), SWP_NOZORDER);
        int by = h - btnH - pad;
        HWND cf = GetDlgItem(IDC_CF), save = GetDlgItem(IDC_SAVE), cancel = GetDlgItem(IDC_CANCEL);
        int cfW = fitWidth(cf, s, 24, 130), saveW = fitWidth(save, s, 24, 80),
            cancelW = fitWidth(cancel, s, 24, 80);
        ::SetWindowPos(cf,     nullptr, pad, by, cfW, btnH, SWP_NOZORDER);
        ::SetWindowPos(cancel, nullptr, w - pad - cancelW, by, cancelW, btnH, SWP_NOZORDER);
        ::SetWindowPos(save,   nullptr, w - 2 * pad - cancelW - saveW, by, saveW, btnH, SWP_NOZORDER);
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
        navidrome::SubsonicClientWin::get().refreshMusicFolders();
        refreshEsLyricBridge();
        ShowWindow(SW_HIDE);
    }

    void OnCancel(UINT, int, HWND) { ShowWindow(SW_HIDE); }

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
            lower += needle;
        };
        ensure("CF-Access-Client-Id");
        ensure("CF-Access-Client-Secret");
        m_edit.SetWindowText(u8ToWide(text).c_str());
        m_edit.SetFocus();
    }
};

class NavidromePrefsInstance : public CWindowImpl<NavidromePrefsInstance>,
                               public preferences_page_instance {
public:
    DECLARE_WND_CLASS(L"foo_navidrome_PrefsWnd")

    explicit NavidromePrefsInstance(preferences_page_callback::ptr cb) : m_cb(cb) {}

    HWND      get_wnd() override { return m_hWnd; }
    t_uint32  get_state() override {
        return preferences_state::dark_mode_supported |
               (m_changed ? preferences_state::changed | preferences_state::resettable : 0);
    }
    void apply()  override {
        saveSettings();
        navidrome::CoverCache::instance().clear();
        navidrome::SubsonicClientWin::get().refreshMusicFolders();
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
        MSG_WM_ERASEBKGND(OnEraseBkgnd)
        MSG_WM_CREATE(OnCreate)
        MSG_WM_SIZE(OnSize)
        MESSAGE_HANDLER_EX(WM_CTLCOLORSTATIC, OnCtlColorStatic)
        MESSAGE_HANDLER_EX(WM_TEST_RESULT, OnTestResult)
        MESSAGE_HANDLER_EX(WM_SCAN_STATUS, OnScanStatus)
        MESSAGE_HANDLER_EX(WM_DIAG_READY, OnDiagReady)
        COMMAND_HANDLER_EX(IDC_URL,  EN_CHANGE, OnChanged)
        COMMAND_HANDLER_EX(IDC_USER, EN_CHANGE, OnChanged)
        COMMAND_HANDLER_EX(IDC_PASS, EN_CHANGE, OnChanged)
        COMMAND_HANDLER_EX(IDC_TEST, BN_CLICKED, OnTest)
        COMMAND_HANDLER_EX(IDC_HEADERS, BN_CLICKED, OnHeaders)
        COMMAND_HANDLER_EX(IDC_SCROBBLE, BN_CLICKED, OnChanged)
        COMMAND_HANDLER_EX(IDC_FORMAT,  CBN_SELCHANGE, OnChanged)
        COMMAND_HANDLER_EX(IDC_BITRATE, CBN_SELCHANGE, OnChanged)
        COMMAND_HANDLER_EX(IDC_RESCAN, BN_CLICKED, OnRescan)
        COMMAND_HANDLER_EX(IDC_DIAG, BN_CLICKED, OnCopyDiagnostics)
        COMMAND_HANDLER_EX(IDC_LOGDIR, BN_CLICKED, OnOpenLogFolder)
    END_MSG_MAP()

    BOOL OnEraseBkgnd(CDCHandle dc) { navidrome::win::eraseLikeDialog(*this, dc); return TRUE; }

    void OnSize(UINT, CSize sz) {
        if (!m_credit[0]) return;
        using namespace navidrome::win;
        const UiScale s(*this);
        const int lineH = lineHeight(*this, uiFont(*this));
        const int pad = s(8);
        int y = (std::max)(m_minCreditY, static_cast<int>(sz.cy) - pad - 2 * lineH);
        for (HWND h : m_credit) {
            ::SetWindowPos(h, nullptr, pad, y, textWidth(h) + s(4), lineH, SWP_NOZORDER);
            y += lineH;
        }
    }

    LRESULT OnCtlColorStatic(UINT msg, WPARAM wp, LPARAM lp) {
        LRESULT brush = DefWindowProc(msg, wp, lp);
        const HWND ctrl = reinterpret_cast<HWND>(lp);
        if (ctrl == m_credit[0] || ctrl == m_credit[1])
            ::SetTextColor(reinterpret_cast<HDC>(wp), ::GetSysColor(COLOR_GRAYTEXT));
        return brush;
    }

private:
    enum { IDC_URL=1001, IDC_USER=1002, IDC_PASS=1003, IDC_TEST=1004, IDC_STATUS=1005,
           IDC_HEADERS=1006, IDC_SCROBBLE=1007, IDC_FORMAT=1008, IDC_BITRATE=1009,
           IDC_RESCAN=1010, IDC_SCAN_STATUS=1011, IDC_DIAG=1012, IDC_LOGDIR=1013,
           IDC_DIAG_STATUS=1014 };

    static constexpr UINT WM_TEST_RESULT = WM_USER + 200;
    static constexpr UINT WM_SCAN_STATUS = WM_USER + 201;
    static constexpr UINT WM_DIAG_READY = WM_USER + 202;

    struct ScanProgress {
        bool        ok       = false;
        bool        done     = false;
        long long   count    = 0;
        std::string error;
    };

    LRESULT OnCreate(LPCREATESTRUCT) {
        using namespace navidrome::win;
        const UiScale s(*this);
        const HFONT f = uiFont(*this);
        auto make = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int id = 0) {
            HWND h = CreateWindowW(cls, text, WS_CHILD|WS_VISIBLE|style, 0,0,0,0, *this,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
            setFont(h, f);
            return h;
        };

        HWND lblUrl  = make(L"STATIC", L"Server URL:",  0);
        HWND lblUser = make(L"STATIC", L"Username:",    0);
        HWND lblPass = make(L"STATIC", L"Password:",    0);
        HWND lblFmt  = make(L"STATIC", L"Stream as:",   0);
        HWND lblRate = make(L"STATIC", L"Max bitrate:", 0);

        const DWORD editSty = WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL;
        HWND url  = make(L"EDIT", L"", editSty, IDC_URL);
        HWND user = make(L"EDIT", L"", editSty, IDC_USER);
        HWND pass = make(L"EDIT", L"", editSty|ES_PASSWORD, IDC_PASS);

        HWND test   = make(L"BUTTON", L"Test Connection", WS_TABSTOP|BS_PUSHBUTTON, IDC_TEST);
        HWND status = make(L"STATIC", L"", SS_LEFT|SS_ENDELLIPSIS, IDC_STATUS);
        HWND hdr    = make(L"BUTTON", L"Custom Headers…", WS_TABSTOP|BS_PUSHBUTTON, IDC_HEADERS);
        HWND scr    = make(L"BUTTON", L"Report plays to Navidrome (scrobbling)",
                           WS_TABSTOP|BS_AUTOCHECKBOX, IDC_SCROBBLE);

        m_format.Create(*this, CWindow::rcDefault, nullptr,
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_VSCROLL|CBS_DROPDOWNLIST, 0, IDC_FORMAT);
        m_format.SetFont(f);
        for (const auto& opt : navidrome::streamFormatOptions())
            m_format.AddString(pfc::stringcvt::string_wide_from_utf8(opt.label));

        m_bitrate.Create(*this, CWindow::rcDefault, nullptr,
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_VSCROLL|CBS_DROPDOWNLIST, 0, IDC_BITRATE);
        m_bitrate.SetFont(f);
        for (int kbps : navidrome::maxBitrateOptions()) {
            m_bitrate.AddString(kbps == 0
                ? L"Unlimited"
                : (std::to_wstring(kbps) + L" kbps").c_str());
        }

        HWND rescan = make(L"BUTTON", L"Rescan", WS_TABSTOP|BS_PUSHBUTTON, IDC_RESCAN);
        HWND scanSt = make(L"STATIC", L"", SS_LEFT|SS_ENDELLIPSIS, IDC_SCAN_STATUS);

        LOGFONTW lf{};
        ::GetObjectW(f, sizeof(lf), &lf);
        lf.lfWeight = FW_BOLD;
        m_sectionFont = ::CreateFontIndirectW(&lf);
        auto makeSection = [&](const wchar_t* title) {
            HWND t = make(L"STATIC", title, SS_LEFT|SS_NOPREFIX);
            setFont(t, m_sectionFont.m_hFont ? m_sectionFont.m_hFont : f);
            HWND line = make(L"STATIC", L"", SS_ETCHEDHORZ);
            return std::make_pair(t, line);
        };
        const auto connSection    = makeSection(L"Navidrome Server Connection");
        const auto librarySection = makeSection(L"Rescan Navidrome Library");
        const auto logsSection    = makeSection(L"Logs and Troubleshooting");

        HWND diag    = make(L"BUTTON", L"Copy Diagnostics", WS_TABSTOP|BS_PUSHBUTTON, IDC_DIAG);
        HWND logDir  = make(L"BUTTON", L"Open Log Folder", WS_TABSTOP|BS_PUSHBUTTON, IDC_LOGDIR);
        HWND diagSt  = make(L"STATIC", L"", SS_LEFT|SS_ENDELLIPSIS, IDC_DIAG_STATUS);

        const int lineH = lineHeight(*this, f);
        const int rowH  = (std::max)(s(22), lineH + s(8));
        const int step  = rowH + s(8);
        const int pad = s(8), gap = s(8);
        int labelW = 0;
        for (HWND l : { lblUrl, lblUser, lblPass, lblFmt, lblRate })
            labelW = (std::max)(labelW, textWidth(l));
        const int x = pad + labelW + gap;
        const int fieldW = s(300), statusW = s(240);
        auto place = [&](HWND h, int px, int py, int w, int hgt) {
            ::SetWindowPos(h, nullptr, px, py, w, hgt, SWP_NOZORDER);
        };
        auto label = [&](HWND l, int rowY) {
            place(l, pad, rowY + (rowH - lineH) / 2, labelW, lineH);
        };
        auto buttonWithStatus = [&](HWND btn, HWND st, int rowY) {
            int w = fitWidth(btn, s, 24, 100);
            place(btn, x, rowY, w, rowH);
            place(st, x + w + gap, rowY + (rowH - lineH) / 2, statusW, lineH);
        };

        auto section = [&](const std::pair<HWND, HWND>& sec, int rowY) {
            const int tw = textWidth(sec.first) + s(4);
            place(sec.first, pad, rowY + (rowH - lineH) / 2, tw, lineH);
            const int lineX = pad + tw + gap;
            place(sec.second, lineX, rowY + rowH / 2, (std::max)(0, x + fieldW - lineX), s(2));
        };

        int y = s(10);
        section(connSection, y);                              y += step;
        label(lblUrl, y);   place(url,  x, y, fieldW, rowH);  y += step;
        label(lblUser, y);  place(user, x, y, fieldW, rowH);  y += step;
        label(lblPass, y);  place(pass, x, y, fieldW, rowH);  y += step;
        buttonWithStatus(test, status, y);                    y += step;
        place(hdr, x, y, fitWidth(hdr, s, 24, 100), rowH);    y += step;
        place(scr, x, y, fitWidth(scr, s, 24), rowH);         y += step;
        label(lblFmt, y);   place(m_format,  x, y, s(240), s(220));  y += step;
        label(lblRate, y);  place(m_bitrate, x, y, s(240), s(220));  y += step;
        y += s(6);
        section(librarySection, y);                           y += step;
        buttonWithStatus(rescan, scanSt, y);                  y += step + s(6);
        section(logsSection, y);                              y += step;
        {
            const int diagW = fitWidth(diag, s, 24, 100);
            place(diag, x, y, diagW, rowH);
            const int logW = fitWidth(logDir, s, 24, 100);
            place(logDir, x + diagW + gap, y, logW, rowH);
            place(diagSt, x + diagW + gap + logW + gap, y + (rowH - lineH) / 2, statusW, lineH);
        }

        m_credit[0] = make(L"STATIC", pfc::stringcvt::string_wide_from_utf8(navidrome::kPrefsAuthorLine), SS_LEFT|SS_NOPREFIX);
        m_credit[1] = make(L"STATIC", pfc::stringcvt::string_wide_from_utf8(navidrome::kSourceCodeUrl), SS_LEFT|SS_NOPREFIX);
        m_minCreditY = y + step + s(4);

        loadSettings();
        m_darkMode.AddDialogWithControls(*this);
        return 0;
    }

    void OnHeaders(UINT, int, HWND) { NavidromeHeadersWindow::get().show(); }

    void loadSettings() {
        SetDlgItemText(IDC_URL,  pfc::stringcvt::string_wide_from_utf8(navidrome::cfg_server_url.get().c_str()));
        SetDlgItemText(IDC_USER, pfc::stringcvt::string_wide_from_utf8(navidrome::cfg_username.get().c_str()));
        SetDlgItemText(IDC_PASS, pfc::stringcvt::string_wide_from_utf8(navidrome::cfg_password.get().c_str()));
        CheckDlgButton(IDC_SCROBBLE, navidrome::cfg_scrobble.get() ? BST_CHECKED : BST_UNCHECKED);

        std::string format = navidrome::cfg_stream_format.get().c_str();
        const auto& formats = navidrome::streamFormatOptions();
        int formatIndex = 0;
        for (std::size_t i = 0; i < formats.size(); ++i)
            if (format == formats[i].value) { formatIndex = static_cast<int>(i); break; }
        m_format.SetCurSel(formatIndex);

        int bitrate = static_cast<int>(navidrome::cfg_max_bitrate.get());
        const auto& bitrates = navidrome::maxBitrateOptions();
        int bitrateIndex = 0;
        for (std::size_t i = 0; i < bitrates.size(); ++i)
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

        const auto& formats = navidrome::streamFormatOptions();
        int fi = m_format.GetCurSel();
        if (fi >= 0 && static_cast<std::size_t>(fi) < formats.size())
            navidrome::cfg_stream_format.set(formats[fi].value);

        const auto& bitrates = navidrome::maxBitrateOptions();
        int bi = m_bitrate.GetCurSel();
        if (bi >= 0 && static_cast<std::size_t>(bi) < bitrates.size())
            navidrome::cfg_max_bitrate.set(bitrates[bi]);
    }

    void OnChanged(UINT, int, HWND) { m_changed = true; notifyCb(); }
    void notifyCb() { if (m_cb.is_valid()) m_cb->on_state_changed(); }

    void OnTest(UINT, int, HWND) {
        saveSettings();
        SetDlgItemText(IDC_STATUS, L"Testing\u2026");
        std::thread([this]() {
          navidrome::dbg::runGuarded("UI", "connection test", [&]{
            std::string err;
            bool ok = navidrome::SubsonicClientWin::get().ping(err);
            PostMessage(WM_TEST_RESULT, ok ? 1 : 0,
                reinterpret_cast<LPARAM>(ok ? nullptr : new std::string(err)));
          });
        }).detach();
    }

    LRESULT OnTestResult(UINT, WPARAM wParam, LPARAM lParam) {
        bool ok = wParam != 0;
        auto* errStr = reinterpret_cast<std::string*>(lParam);
        SetDlgItemText(IDC_STATUS, ok ? L"Connected!" :
            pfc::stringcvt::string_wide_from_utf8(errStr ? errStr->c_str() : "Failed"));
        delete errStr;
        return 0;
    }

    void OnCopyDiagnostics(UINT, int, HWND) {
        ::EnableWindow(GetDlgItem(IDC_DIAG), FALSE);
        SetDlgItemText(IDC_DIAG_STATUS, L"Collecting\u2026");
        const HWND page = m_hWnd;
        std::thread([page]() {
          navidrome::dbg::runGuarded("UI", "copy diagnostics", [&]{
            auto* text = new std::string(navidrome::collectDiagnostics());
            if (!::PostMessage(page, WM_DIAG_READY, 0, reinterpret_cast<LPARAM>(text))) delete text;
          });
        }).detach();
    }

    LRESULT OnDiagReady(UINT, WPARAM, LPARAM lParam) {
        std::unique_ptr<std::string> text(reinterpret_cast<std::string*>(lParam));
        ::EnableWindow(GetDlgItem(IDC_DIAG), TRUE);
        const bool ok = text && copyToClipboard(pfc::stringcvt::string_wide_from_utf8(text->c_str()).get_ptr());
        if (!ok) NAVIDROME_WARN("UI", "copy diagnostics: clipboard unavailable");
        SetDlgItemText(IDC_DIAG_STATUS, ok ? L"Copied to the clipboard"
                                           : L"Couldn't open the clipboard");
        return 0;
    }

    bool copyToClipboard(const std::wstring& w) {
        if (!::OpenClipboard(*this)) return false;
        ::EmptyClipboard();
        const size_t bytes = (w.size() + 1) * sizeof(wchar_t);
        HGLOBAL mem = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
        bool ok = false;
        if (mem) {
            if (void* p = ::GlobalLock(mem)) {
                memcpy(p, w.c_str(), bytes);
                ::GlobalUnlock(mem);
                ok = ::SetClipboardData(CF_UNICODETEXT, mem) != nullptr;
            }
            if (!ok) ::GlobalFree(mem);
        }
        ::CloseClipboard();
        return ok;
    }

    void OnOpenLogFolder(UINT, int, HWND) {
        const std::string path = navidrome::componentLogPath();
        if (path.empty()) { SetDlgItemText(IDC_DIAG_STATUS, L"No log file yet"); return; }
        const std::wstring wpath = pfc::stringcvt::string_wide_from_utf8(path.c_str()).get_ptr();
        std::wstring args;
        if (::GetFileAttributesW(wpath.c_str()) != INVALID_FILE_ATTRIBUTES) {
            args = L"/select,\"" + wpath + L"\"";
        } else {
            const size_t slash = wpath.find_last_of(L"\\/");
            args = L"\"" + wpath.substr(0, slash == std::wstring::npos ? 0 : slash) + L"\"";
        }
        ::ShellExecuteW(*this, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
    }

    void OnRescan(UINT, int, HWND) {
        ::EnableWindow(GetDlgItem(IDC_RESCAN), FALSE);
        SetDlgItemText(IDC_SCAN_STATUS, L"Starting scan…");

        std::thread([this]() {
          navidrome::dbg::runGuarded("UI", "library rescan poll", [&]{
            std::string err;
            auto status = navidrome::SubsonicClientWin::get().startScan(err);
            if (!err.empty()) {
                PostMessage(WM_SCAN_STATUS,
                    reinterpret_cast<WPARAM>(new ScanProgress{false, true, 0, err}), 0);
                return;
            }

            while (status.scanning) {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(navidrome::kScanPollIntervalMs));
                std::string pollErr;
                auto polled = navidrome::SubsonicClientWin::get().getScanStatus(pollErr);
                if (!pollErr.empty()) break;
                status = polled;
                PostMessage(WM_SCAN_STATUS,
                    reinterpret_cast<WPARAM>(new ScanProgress{true, false, status.count, ""}), 0);
            }
            PostMessage(WM_SCAN_STATUS,
                reinterpret_cast<WPARAM>(new ScanProgress{true, true, status.count, ""}), 0);
          });
        }).detach();
    }

    LRESULT OnScanStatus(UINT, WPARAM wParam, LPARAM) {
        auto* p = reinterpret_cast<ScanProgress*>(wParam);
        if (!p->ok) {
            SetDlgItemText(IDC_SCAN_STATUS,
                pfc::stringcvt::string_wide_from_utf8(("Scan failed: " + p->error).c_str()));
        } else if (p->done) {
            SetDlgItemText(IDC_SCAN_STATUS,
                (L"Scan complete — " + std::to_wstring(p->count) + L" items").c_str());
        } else {
            SetDlgItemText(IDC_SCAN_STATUS,
                (L"Scanning… " + std::to_wstring(p->count) + L" processed").c_str());
        }
        if (p->done) ::EnableWindow(GetDlgItem(IDC_RESCAN), TRUE);
        delete p;
        return 0;
    }

    CComboBox m_format, m_bitrate;
    HWND m_credit[2] = {};
    CFont m_sectionFont;
    int  m_minCreditY = 0;
    fb2k::CCoreDarkModeHooks m_darkMode;
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

class AudioMusePrefsInstance : public CWindowImpl<AudioMusePrefsInstance>,
                               public preferences_page_instance {
public:
    DECLARE_WND_CLASS(L"foo_navidrome_AudioMusePrefsWnd")

    explicit AudioMusePrefsInstance(preferences_page_callback::ptr cb) : m_cb(cb) {}

    HWND     get_wnd() override { return m_hWnd; }
    t_uint32 get_state() override {
        return preferences_state::dark_mode_supported |
               (m_changed ? preferences_state::changed | preferences_state::resettable : 0);
    }
    void apply() override {
        auto getText = [&](int id) -> std::string {
            wchar_t buf[2048] = {};
            GetDlgItemText(id, buf, 2048);
            return pfc::stringcvt::string_utf8_from_wide(buf).get_ptr();
        };
        navidrome::cfg_audiomuse_url.set(getText(IDC_AM_URL).c_str());
        navidrome::cfg_audiomuse_token.set(getText(IDC_AM_TOKEN).c_str());
        navidrome::cfg_audiomuse_server.set(getText(IDC_AM_SERVER).c_str());
        navidrome::cfg_audiomuse_count.set(
            navidrome::audiomuse::clampCount(atoi(getText(IDC_AM_COUNT).c_str())));
        NAVIDROME_LOG("UI", "AudioMuse-AI settings saved, url=" +
                      navidrome::dbg::scrubAuth(navidrome::cfg_audiomuse_url.get().c_str()));
        load();
        m_changed = false;
        notifyCb();
    }
    void reset() override {
        SetDlgItemText(IDC_AM_URL, L"");
        SetDlgItemText(IDC_AM_TOKEN, L"");
        SetDlgItemText(IDC_AM_SERVER, L"");
        SetDlgItemText(IDC_AM_COUNT, std::to_wstring(navidrome::audiomuse::kDefaultCount).c_str());
        m_changed = true; notifyCb();
    }

    BEGIN_MSG_MAP(AudioMusePrefsInstance)
        MSG_WM_ERASEBKGND(OnEraseBkgnd)
        MSG_WM_CREATE(OnCreate)
        COMMAND_HANDLER_EX(IDC_AM_URL,    EN_CHANGE, OnChanged)
        COMMAND_HANDLER_EX(IDC_AM_TOKEN,  EN_CHANGE, OnChanged)
        COMMAND_HANDLER_EX(IDC_AM_SERVER, EN_CHANGE, OnChanged)
        COMMAND_HANDLER_EX(IDC_AM_COUNT,  EN_CHANGE, OnChanged)
    END_MSG_MAP()

    BOOL OnEraseBkgnd(CDCHandle dc) { navidrome::win::eraseLikeDialog(*this, dc); return TRUE; }

private:
    enum { IDC_AM_URL = 1101, IDC_AM_TOKEN = 1102, IDC_AM_SERVER = 1103, IDC_AM_COUNT = 1104 };

    LRESULT OnCreate(LPCREATESTRUCT) {
        using namespace navidrome::win;
        const UiScale s(*this);
        const HFONT f = uiFont(*this);
        auto make = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int id = 0) {
            HWND h = CreateWindowW(cls, text, WS_CHILD|WS_VISIBLE|style, 0,0,0,0, *this,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
            setFont(h, f);
            return h;
        };
        HWND intro = make(L"STATIC",
            L"AudioMuse-AI analyses your Navidrome library for Text Search, Instant "
            L"Playlist and Song Alchemy (File › AudioMuse-AI, track context menu).", SS_LEFT);
        const DWORD editSty = WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL;
        const std::pair<HWND, HWND> rows[] = {
            { make(L"STATIC", L"Server URL:",  0), make(L"EDIT", L"", editSty, IDC_AM_URL) },
            { make(L"STATIC", L"API token:",   0), make(L"EDIT", L"", editSty|ES_PASSWORD, IDC_AM_TOKEN) },
            { make(L"STATIC", L"Server name:", 0), make(L"EDIT", L"", editSty, IDC_AM_SERVER) },
            { make(L"STATIC", L"Tracks:",      0), make(L"EDIT", L"", editSty|ES_NUMBER, IDC_AM_COUNT) },
        };
        HWND help = make(L"STATIC",
            L"e.g. http://audiomuse:8000. Token only if AudioMuse-AI has auth on; "
            L"server name only if it serves several media servers. \"Tracks\" also "
            L"sizes Instant Mix, which works without AudioMuse-AI settings.", SS_LEFT);

        const int lineH = lineHeight(*this, f);
        const int rowH  = (std::max)(s(22), lineH + s(8));
        const int pad = s(8), gap = s(8), textW = s(440);
        int labelW = 0;
        for (const auto& r : rows) labelW = (std::max)(labelW, textWidth(r.first));
        const int x = pad + labelW + gap;

        int y = pad;
        ::SetWindowPos(intro, nullptr, pad, y, textW, 3 * lineH, SWP_NOZORDER);
        y += 3 * lineH + s(8);
        for (const auto& r : rows) {
            int w = r.second == rows[3].second ? s(60) : s(300);
            ::SetWindowPos(r.first, nullptr, pad, y + (rowH - lineH) / 2, labelW, lineH, SWP_NOZORDER);
            ::SetWindowPos(r.second, nullptr, x, y, w, rowH, SWP_NOZORDER);
            y += rowH + s(8);
        }
        ::SetWindowPos(help, nullptr, pad, y + s(4), textW, 4 * lineH, SWP_NOZORDER);

        load();
        m_darkMode.AddDialogWithControls(*this);
        return 0;
    }

    void load() {
        SetDlgItemText(IDC_AM_URL,    pfc::stringcvt::string_wide_from_utf8(navidrome::cfg_audiomuse_url.get().c_str()));
        SetDlgItemText(IDC_AM_TOKEN,  pfc::stringcvt::string_wide_from_utf8(navidrome::cfg_audiomuse_token.get().c_str()));
        SetDlgItemText(IDC_AM_SERVER, pfc::stringcvt::string_wide_from_utf8(navidrome::cfg_audiomuse_server.get().c_str()));
        SetDlgItemText(IDC_AM_COUNT,  std::to_wstring(navidrome::audiomuse::clampCount(
                           static_cast<int>(navidrome::cfg_audiomuse_count.get()))).c_str());
    }

    void OnChanged(UINT, int, HWND) { m_changed = true; notifyCb(); }
    void notifyCb() { if (m_cb.is_valid()) m_cb->on_state_changed(); }

public:
    void clearChanged() { m_changed = false; notifyCb(); }

private:
    fb2k::CCoreDarkModeHooks m_darkMode;
    preferences_page_callback::ptr m_cb;
    bool m_changed = false;
};

class AudioMusePrefsPageFactory : public preferences_page_v3 {
public:
    preferences_page_instance::ptr instantiate(HWND parent,
        preferences_page_callback::ptr cb) override {
        auto inst = fb2k::service_new<AudioMusePrefsInstance>(cb);
        inst->Create(parent);
        inst->clearChanged();
        return inst;
    }
    const char* get_name() override { return "AudioMuse-AI"; }
    GUID        get_guid() override {
        return { 0xa1b2c3d4,0x1111,0x2222,{0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x04,0x05} };
    }
    GUID        get_parent_guid() override { return guid_prefs_page; }
};
FB2K_SERVICE_FACTORY(AudioMusePrefsPageFactory);

class NavidromeLibraryPrefsInstance : public CWindowImpl<NavidromeLibraryPrefsInstance>,
                                      public preferences_page_instance {
public:
    DECLARE_WND_CLASS(L"foo_navidrome_LibPrefsWnd")

    explicit NavidromeLibraryPrefsInstance(preferences_page_callback::ptr cb) : m_cb(cb) {}

    HWND      get_wnd() override { return m_hWnd; }
    t_uint32  get_state() override { return preferences_state::dark_mode_supported; }
    void      apply() override {}
    void      reset() override {}

    BEGIN_MSG_MAP(NavidromeLibraryPrefsInstance)
        MSG_WM_CREATE(OnCreate)
        MSG_WM_SIZE(OnSize)
    END_MSG_MAP()

private:
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
    GUID        get_guid() override {
        return { 0xa1b2c3d4,0x1111,0x2222,{0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x01,0x09} };
    }
    GUID        get_parent_guid() override { return preferences_page::guid_media_library; }
};
FB2K_SERVICE_FACTORY(NavidromeLibraryPrefsFactory);

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
          navidrome::dbg::runGuarded("Bookmark", "bookmarkCurrentPosition", [&]{
            std::string err;
            navidrome::SubsonicClientWin::get().createBookmark(songId, positionMs, "", err);
            if (!err.empty())
                console::printf("Navidrome: failed to save bookmark: %s", err.c_str());
            NAVIDROME_LOG("Bookmark", "save id=" + songId + " pos=" +
                std::to_string((long long)positionMs) + "ms" +
                (err.empty() ? " ok" : " FAILED: " + err));
          });
        }).detach();
    }
};
FB2K_SERVICE_FACTORY(NavidromeMenuCmd);

namespace {

    std::mutex g_coverDiagMutex;
    std::set<std::pair<navidrome::FetchClass, std::string>> g_coverDiagSeen;

    void logCoverError(navidrome::FetchClass cls, const std::string& id) {
        using namespace navidrome;
        if (cls == FetchClass::NotFound) return;

        {
            std::lock_guard<std::mutex> lock(g_coverDiagMutex);
            if (!g_coverDiagSeen.insert({cls, id}).second) return;
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

        auto cached = navidrome::CoverCache::instance().get(
            m_context.serverUrl, m_context.username, m_id);
        if (!cached.empty()) {
            return album_art_data_impl::g_create(cached.data(), cached.size());
        }

        std::string url = navidrome::SubsonicClientWin::get().coverArtURL(
            m_context, m_id, 0);
        static constexpr std::size_t kMaxCoverBytes = 20 * 1024 * 1024;

        auto result = navidrome::SubsonicClientWin::get().httpGetBinary(
            m_context, url, kMaxCoverBytes, abort);

        if (result.cls == navidrome::FetchClass::Aborted) {
            throw exception_aborted();
        }

        if (result.cls != navidrome::FetchClass::Ok) {
            logCoverError(result.cls, m_id);
            throw exception_album_art_not_found();
        }

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
        return navidrome::isNavidromeArtPath(p);
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

class NavidromeScrobbler : public play_callback_static {
public:
    unsigned get_flags() override {
        return flag_on_playback_new_track | flag_on_playback_time |
               flag_on_playback_stop;
    }

    void on_playback_new_track(metadb_handle_ptr track) override {
        auto a = m_tracker.onNewTrack(
            track.is_empty() ? std::string() : std::string(track->get_path()),
            track.is_empty() ? 0.0 : track->get_length(),
            navidrome::cfg_scrobble.get());
        if (!a.refreshRatingId.empty()) refreshRatingAsync(a.refreshRatingId);
        if (!a.scrobbleNowId.empty())   scrobbleAsync(a.scrobbleNowId, false);
    }

    void on_playback_time(double time) override {
        std::string id = m_tracker.onPlaybackTime(time);
        if (!id.empty()) scrobbleAsync(id, true);
    }

    void on_playback_stop(play_control::t_stop_reason) override {
        m_tracker.onStop();
    }

    void on_playback_starting(play_control::t_track_command, bool) override {}
    void on_playback_seek(double) override {}
    void on_playback_pause(bool) override {}
    void on_playback_edited(metadb_handle_ptr) override {}
    void on_playback_dynamic_info(const file_info&) override {}
    void on_playback_dynamic_info_track(const file_info&) override {}
    void on_volume_change(float) override {}

private:
    static void scrobbleAsync(std::string songId, bool submission) {
        std::thread([songId, submission]() {
            navidrome::dbg::runGuarded("Scrobble", "scrobbleAsync", [&]{
                std::string err;
                navidrome::SubsonicClientWin::get().scrobble(songId, submission, err);
                navidrome::Error e = navidrome::SubsonicClientWin::get().lastError();
                NAVIDROME_LOG("Scrobble", std::string(submission ? "submit" : "now-playing") +
                              " id=" + songId + (e.ok() ? " ok"
                              : std::string(" FAILED ") + e.kindName() + ": " + e.message));
            });
        }).detach();
    }

    static void refreshRatingAsync(std::string songId) {
        std::thread([songId]() {
            navidrome::dbg::runGuarded("Rating", "refreshRatingAsync", [&]{
                std::string err;
                navidrome::Song song;
                if (!navidrome::SubsonicClientWin::get().getSong(songId, song, err)) {
                    NAVIDROME_WARN("Rating", "getSong id=" + songId + " failed: " +
                        navidrome::SubsonicClientWin::get().lastError().kindName());
                    return;
                }
                navidrome::RatingUpdate u;
                u.songId  = songId;
                u.rating  = song.rating;
                u.starred = song.starred;
                std::vector<navidrome::RatingUpdate> updates;
                updates.push_back(std::move(u));
                navidrome::syncRatingsToPlaylists(std::move(updates));
                NAVIDROME_LOG("Rating", "id=" + songId + " -> rating=" +
                    std::to_string(u.rating) + " starred=" + (u.starred ? "1" : "0"));
            });
        }).detach();
    }

    navidrome::ScrobbleTracker m_tracker;
};
static play_callback_static_factory_t<NavidromeScrobbler> g_navidrome_scrobbler_factory;

bool navidrome::setRatingOnServer(const std::string& songId, int rating) {
    std::string err;
    return navidrome::SubsonicClientWin::get().setRating(rating, songId, err);
}

bool navidrome::setStarredOnServer(const std::string& songId, bool starred) {
    std::string err;
    return navidrome::SubsonicClientWin::get().setStarred(
        starred, songId, navidrome::StarKind::Song, err);
}

static void navidromeLogSessionEnv() {
#ifdef NAVIDROME_DEBUG_LOG
    navidrome::SessionEnv e;
    e.platform        = "Windows";
    e.configured      = navidrome::SubsonicClientWin::get().isConfigured();
    e.serverUrl       = navidrome::cfg_server_url.get().c_str();
    e.transcodeFormat = navidrome::cfg_stream_format.get().c_str();
    e.maxBitrate      = (int)navidrome::cfg_max_bitrate.get();
    e.scrobble        = navidrome::cfg_scrobble.get();
    e.startupRefresh  = navidrome::refreshRatingsOnStartEnabled();
    e.customHeaders   = navidrome::cfg_custom_headers.get().length() > 0;
    NAVIDROME_LOG("Env", navidrome::describeSessionEnv(e));
#endif
}

static void navidromeRefreshRatingsOnStart() {
    navidromeLogSessionEnv();

    navidrome::PlaylistAlbumScan scan = navidrome::scanPlaylistAlbums();

    if (scan.entries == 0) return;

    if (!navidrome::refreshRatingsOnStartEnabled()) {
        NAVIDROME_LOG("Rating", "startup refresh: disabled by advconfig switch");
        return;
    }
    if (!navidrome::SubsonicClientWin::get().isConfigured()) {
        NAVIDROME_LOG("Rating", "startup refresh: no server configured");
        return;
    }
    NAVIDROME_LOG("Rating", "startup refresh: " + std::to_string(scan.entries) +
                  " entries, " + std::to_string(scan.albumIds.size()) + " distinct albums, " +
                  std::to_string(scan.ungrouped) + " ungrouped");

    if (scan.albumIds.empty()) {
        std::string msg = "Navidrome: " + std::to_string(scan.entries) +
            " playlist entry/entries carry no album id (added by an older version)"
            " — open their album in the browser to refresh them";
        console::print(msg.c_str());
        return;
    }

    const std::size_t ungrouped = scan.ungrouped;
    std::thread([albumIds = std::move(scan.albumIds), ungrouped]() {
      navidrome::dbg::runGuarded("Rating", "startup refresh worker", [&]{
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
        const std::size_t ok = albumIds.size() - failed;
        navidrome::syncRatingsToPlaylists(std::move(updates));

        std::string msg = "Navidrome: refreshed ratings from " + std::to_string(ok) + " album(s)";
        if (failed > 0)    msg += ", " + std::to_string(failed) + " album(s) failed";
        if (ungrouped > 0) msg += ", " + std::to_string(ungrouped) +
            " entry/entries skipped (no album id, added by an older version)";
        console::print(msg.c_str());
        NAVIDROME_LOG("Rating", "startup refresh done: " + msg);
      });
    }).detach();
}

class NavidromeStartupRefresh : public initquit {
public:
    void on_init() override { navidromeRefreshRatingsOnStart(); }
};

static initquit_factory_t<NavidromeStartupRefresh> g_navidrome_startup_refresh_factory;

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
