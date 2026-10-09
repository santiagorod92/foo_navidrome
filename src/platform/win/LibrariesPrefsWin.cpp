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

namespace navidrome {

    extern cfg_var_modern::cfg_bool cfg_library_filter;
    extern cfg_string cfg_library_ids;
}

namespace {

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

        navidrome::win::addDarkModeHooksKeepingLists(m_darkMode, *this);
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
                delete payload;
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

    void readSelectionFromChecks() {
        std::vector<std::string> ids;
        for (int i = 0; i < static_cast<int>(m_folders.size()); ++i)
            if (m_list.GetCheckState(i)) ids.push_back(m_folders[static_cast<std::size_t>(i)].id);
        m_selected = std::move(ids);
    }

    void OnToggleEnable(UINT, int, HWND) {
        m_stagedEnabled = IsDlgButtonChecked(IDC_ENABLE) == BST_CHECKED;
        if (!m_stagedEnabled) {
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
    std::vector<std::string> m_selected;
    std::vector<std::string> m_savedIds;
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
}
