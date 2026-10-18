#include "stdafx.h"
#include "BrowserWindow.h"
#include "SubsonicClientWin.h"
#include "../../core/MediaEnrichmentLogic.h"
#include "../../core/NavidromePlaylistSync.h"
#include "../../core/NavidromeDebugLog.h"
#include "../../core/NavidromeAudioMuse.h"
#include "../../core/NavidromeDiagnostics.h"
#include "../../core/NavidromeGuids.h"
#include "../../core/NavidromeSettings.h"
#include "../../core/NavidromeLibraryPlatform.h"
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
            console::print(navidrome::trf("ESLyric bridge error: {0}", { err }).c_str());
    }
}

using navidrome::win::u8ToWide;
using navidrome::win::wToU8;
using navidrome::win::trw;

class NavidromeHeadersWindow : public CWindowImpl<NavidromeHeadersWindow> {
public:
    DECLARE_WND_CLASS(L"foo_navidrome_HeadersWnd")

    static NavidromeHeadersWindow& get() { static NavidromeHeadersWindow inst; return inst; }

    void show() {
        if (!IsWindow()) {
            Create(nullptr, CWindow::rcDefault, trw("Navidrome — Custom HTTP Headers").c_str(),
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
            trw("One header per line, as  Name: Value  (e.g. for a Cloudflare Zero Trust tunnel).").c_str(),
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
        mkBtn(IDC_CF,     trw("Cloudflare headers").c_str());
        mkBtn(IDC_SAVE,   trw("Save").c_str());
        mkBtn(IDC_CANCEL, trw("Cancel").c_str());
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
        m_language.SetCurSel(0);
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
        COMMAND_HANDLER_EX(IDC_LANGUAGE, CBN_SELCHANGE, OnChanged)
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
           IDC_DIAG_STATUS=1014, IDC_LANGUAGE=1015 };

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

        HWND lblUrl  = make(L"STATIC", trw("Server URL:").c_str(),  0);
        HWND lblUser = make(L"STATIC", trw("Username:").c_str(),    0);
        HWND lblPass = make(L"STATIC", trw("Password:").c_str(),    0);
        HWND lblFmt  = make(L"STATIC", trw("Stream as:").c_str(),   0);
        HWND lblRate = make(L"STATIC", trw("Max bitrate:").c_str(), 0);
        HWND lblLang = make(L"STATIC", trw("Language:").c_str(), 0);

        const DWORD editSty = WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL;
        HWND url  = make(L"EDIT", L"", editSty, IDC_URL);
        HWND user = make(L"EDIT", L"", editSty, IDC_USER);
        HWND pass = make(L"EDIT", L"", editSty|ES_PASSWORD, IDC_PASS);

        HWND test   = make(L"BUTTON", trw("Test Connection").c_str(), WS_TABSTOP|BS_PUSHBUTTON, IDC_TEST);
        HWND status = make(L"STATIC", L"", SS_LEFT|SS_ENDELLIPSIS, IDC_STATUS);
        HWND hdr    = make(L"BUTTON", trw("Custom Headers…").c_str(), WS_TABSTOP|BS_PUSHBUTTON, IDC_HEADERS);
        HWND scr    = make(L"BUTTON", trw("Report plays to Navidrome (scrobbling)").c_str(),
                           WS_TABSTOP|BS_AUTOCHECKBOX, IDC_SCROBBLE);

        m_format.Create(*this, CWindow::rcDefault, nullptr,
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_VSCROLL|CBS_DROPDOWNLIST, 0, IDC_FORMAT);
        m_format.SetFont(f);
        for (const auto& opt : navidrome::streamFormatOptions())
            m_format.AddString(trw(opt.label).c_str());

        m_bitrate.Create(*this, CWindow::rcDefault, nullptr,
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_VSCROLL|CBS_DROPDOWNLIST, 0, IDC_BITRATE);
        m_bitrate.SetFont(f);
        for (int kbps : navidrome::maxBitrateOptions()) {
            m_bitrate.AddString(kbps == 0
                ? trw("Unlimited").c_str()
                : (std::to_wstring(kbps) + L" kbps").c_str());
        }

        m_language.Create(*this, CWindow::rcDefault, nullptr,
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_VSCROLL|CBS_DROPDOWNLIST, 0, IDC_LANGUAGE);
        m_language.SetFont(f);
        for (const auto& opt : navidrome::i18n::languageOptions())
            m_language.AddString(trw(opt.label).c_str());
        HWND langHint = make(L"STATIC", trw("Restart foobar2000 to apply.").c_str(), SS_LEFT|SS_NOPREFIX);

        HWND rescan = make(L"BUTTON", trw("Rescan").c_str(), WS_TABSTOP|BS_PUSHBUTTON, IDC_RESCAN);
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
        const auto connSection    = makeSection(trw("Navidrome Server Connection").c_str());
        const auto librarySection = makeSection(trw("Rescan Navidrome Library").c_str());
        const auto logsSection    = makeSection(trw("Logs and Troubleshooting").c_str());

        HWND diag    = make(L"BUTTON", trw("Copy Diagnostics").c_str(), WS_TABSTOP|BS_PUSHBUTTON, IDC_DIAG);
        HWND logDir  = make(L"BUTTON", trw("Open Log Folder").c_str(), WS_TABSTOP|BS_PUSHBUTTON, IDC_LOGDIR);
        HWND diagSt  = make(L"STATIC", L"", SS_LEFT|SS_ENDELLIPSIS, IDC_DIAG_STATUS);

        const int lineH = lineHeight(*this, f);
        const int rowH  = (std::max)(s(22), lineH + s(8));
        const int step  = rowH + s(8);
        const int pad = s(8), gap = s(8);
        int labelW = 0;
        for (HWND l : { lblUrl, lblUser, lblPass, lblFmt, lblRate, lblLang })
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
        label(lblLang, y);  place(m_language, x, y, s(240), s(220));  y += rowH + s(4);
        place(langHint, x, y, textWidth(langHint) + s(4), lineH);       y += lineH + s(8);
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

        m_credit[0] = make(L"STATIC", trw(navidrome::kPrefsAuthorLine).c_str(), SS_LEFT|SS_NOPREFIX);
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

        m_language.SetCurSel(navidrome::i18n::languageOptionIndex(navidrome::cfg_ui_language.get().c_str()));
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

        const auto& langs = navidrome::i18n::languageOptions();
        const int li = m_language.GetCurSel();
        if (li >= 0 && static_cast<std::size_t>(li) < langs.size()) {
            if (navidrome::cfg_ui_language.get() != langs[li].value)
                NAVIDROME_NOTE("Env", std::string("UI language set to \"") + langs[li].value + "\" (applies after restart)");
            navidrome::cfg_ui_language.set(langs[li].value);
        }
    }

    void OnChanged(UINT, int, HWND) { m_changed = true; notifyCb(); }
    void notifyCb() { if (m_cb.is_valid()) m_cb->on_state_changed(); }

    void OnTest(UINT, int, HWND) {
        saveSettings();
        SetDlgItemText(IDC_STATUS, trw("Testing…").c_str());
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
        SetDlgItemText(IDC_STATUS, ok ? trw("Connected!").c_str() :
            pfc::stringcvt::string_wide_from_utf8(errStr ? errStr->c_str() : navidrome::tr("Failed")));
        delete errStr;
        return 0;
    }

    void OnCopyDiagnostics(UINT, int, HWND) {
        ::EnableWindow(GetDlgItem(IDC_DIAG), FALSE);
        SetDlgItemText(IDC_DIAG_STATUS, trw("Collecting…").c_str());
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
        SetDlgItemText(IDC_DIAG_STATUS, ok ? trw("Copied to the clipboard").c_str()
                                           : trw("Couldn't open the clipboard").c_str());
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
        if (path.empty()) { SetDlgItemText(IDC_DIAG_STATUS, trw("No log file yet").c_str()); return; }
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
        SetDlgItemText(IDC_SCAN_STATUS, trw("Starting scan…").c_str());

        std::thread([this]() {
          navidrome::dbg::runGuarded("UI", "library rescan poll", [&]{
            std::string err;
            auto status = navidrome::libraryCore().runLibraryScan([this](const navidrome::ScanStatus& st) {
                PostMessage(WM_SCAN_STATUS,
                    reinterpret_cast<WPARAM>(new ScanProgress{true, false, st.count, ""}), 0);
            }, err);
            PostMessage(WM_SCAN_STATUS,
                reinterpret_cast<WPARAM>(new ScanProgress{err.empty(), true, status.count, err}), 0);
          });
        }).detach();
    }

    LRESULT OnScanStatus(UINT, WPARAM wParam, LPARAM) {
        auto* p = reinterpret_cast<ScanProgress*>(wParam);
        const std::string text = !p->ok   ? navidrome::scanFailedText(p->error)
                               : p->done ? navidrome::scanCompleteText(p->count)
                                         : navidrome::scanProgressText(p->count);
        SetDlgItemText(IDC_SCAN_STATUS, u8ToWide(text).c_str());
        if (p->done) ::EnableWindow(GetDlgItem(IDC_RESCAN), TRUE);
        delete p;
        return 0;
    }

    CComboBox m_format, m_bitrate, m_language;
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
    GUID        get_guid() override { return navidrome::guids::prefsPage; }
    GUID        get_parent_guid() override { return preferences_page::guid_tools; }
};
FB2K_SERVICE_FACTORY(NavidromePrefsPageFactory);

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
        return navidrome::guids::libraryPrefsPage;
    }
    GUID        get_parent_guid() override { return preferences_page::guid_media_library; }
};
FB2K_SERVICE_FACTORY(NavidromeLibraryPrefsFactory);


class NavidromeInitQuit : public initquit {
public:
    void on_init() override {
        if (!navidrome::EsLyricBridge::isEsLyricInstalled()) {
            console::print(navidrome::tr("ESLyric not detected (playback unaffected)"));
            return;
        }
        refreshEsLyricBridge();
    }

    void on_quit() override {}
};
FB2K_SERVICE_FACTORY(NavidromeInitQuit);
