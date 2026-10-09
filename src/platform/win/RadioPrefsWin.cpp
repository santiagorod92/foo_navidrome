#include "stdafx.h"
#include "BrowserPrompts.h"
#include "SubsonicClientWin.h"
#include "WinUi.h"
#include "../../core/NavidromeDebugLog.h"
#include <SDK/cfg_var.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <string>
#include <thread>
#include <vector>

using navidrome::win::u8ToWide;
using navidrome::win::wToU8;

namespace {

class NavidromeRadioPrefsInstance : public CWindowImpl<NavidromeRadioPrefsInstance>,
                                    public preferences_page_instance {
public:
    DECLARE_WND_CLASS(L"foo_navidrome_RadioPrefsWnd")

    explicit NavidromeRadioPrefsInstance(preferences_page_callback::ptr cb) : m_cb(cb) {}

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

        navidrome::win::addDarkModeHooksKeepingLists(m_darkMode, *this);
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
                delete payload;
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
        if (!navidrome::win::promptRadioStation(*this, L"New Radio Station", L"", L"", L"",
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
            if (!ok) NAVIDROME_WARN("UI", "create radio station \"" + nameU8 + "\": " + err);
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
        if (!navidrome::win::promptRadioStation(*this, L"Edit Radio Station",
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
            if (!ok) NAVIDROME_WARN("UI", "update radio station " + id + ": " + err);
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
            if (!ok) NAVIDROME_WARN("UI", "delete radio station " + id + ": " + err);
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
    GUID        get_parent_guid() override {
        return { 0xa1b2c3d4,0x1111,0x2222,{0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x01,0x05} };
    }
};
FB2K_SERVICE_FACTORY(NavidromeRadioPrefsFactory);
}
