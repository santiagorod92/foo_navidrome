#include "stdafx.h"
#include "WinUi.h"
#include "../../core/NavidromeAudioMuse.h"
#include "../../core/NavidromeDebugLog.h"
#include "../../core/NavidromeGuids.h"
#include <string>

using navidrome::win::trw;

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
            trw("AudioMuse-AI analyses your Navidrome library for Text Search, Instant "
                "Playlist and Song Alchemy (File › AudioMuse-AI, track context menu).").c_str(), SS_LEFT);
        const DWORD editSty = WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL;
        const std::pair<HWND, HWND> rows[] = {
            { make(L"STATIC", trw("Server URL:").c_str(),  0), make(L"EDIT", L"", editSty, IDC_AM_URL) },
            { make(L"STATIC", trw("API token:").c_str(),   0), make(L"EDIT", L"", editSty|ES_PASSWORD, IDC_AM_TOKEN) },
            { make(L"STATIC", trw("Server name:").c_str(), 0), make(L"EDIT", L"", editSty, IDC_AM_SERVER) },
            { make(L"STATIC", trw("Tracks:").c_str(),      0), make(L"EDIT", L"", editSty|ES_NUMBER, IDC_AM_COUNT) },
        };
        HWND help = make(L"STATIC",
            trw("e.g. http://audiomuse:8000. Token only if AudioMuse-AI has auth on; "
                "server name only if it serves several media servers. \"Tracks\" also "
                "sizes Instant Mix, which works without AudioMuse-AI settings.").c_str(), SS_LEFT);

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
        return navidrome::guids::audioMusePrefsPage;
    }
    GUID        get_parent_guid() override { return navidrome::guids::prefsPage; }
};
FB2K_SERVICE_FACTORY(AudioMusePrefsPageFactory);
