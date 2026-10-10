#include "stdafx.h"
#include "BrowserWindow.h"
#include "WinUi.h"
#include "../../core/NavidromeBrowserModel.h"
#include "../../core/NavidromeDebugLog.h"
#include <SDK/cfg_var.h>
#include <string>
#include <vector>

using navidrome::win::u8ToWide;

namespace navidrome {

    extern cfg_string cfg_browser_hidden_categories;
}

namespace {

class NavidromeSectionsPrefsInstance : public CWindowImpl<NavidromeSectionsPrefsInstance>,
                                       public preferences_page_instance {
public:
    DECLARE_WND_CLASS(L"foo_navidrome_SectionsPrefsWnd")

    explicit NavidromeSectionsPrefsInstance(preferences_page_callback::ptr cb) : m_cb(cb) {}

    HWND     get_wnd() override { return m_hWnd; }
    t_uint32 get_state() override {
        return preferences_state::dark_mode_supported |
               (m_changed ? preferences_state::changed : 0) |
               (readHidden().empty() ? 0 : preferences_state::resettable);
    }
    void apply() override {
        const std::string csv = navidrome::joinHiddenCategories(readHidden());
        navidrome::cfg_browser_hidden_categories.set(csv.c_str());
        NAVIDROME_LOG("UI", "browser sections: hidden = [" + csv + "]");
        m_saved = csv;
        m_changed = false;
        notifyCb();
        BrowserWindow::reloadAllOpen();
    }
    void reset() override {
        for (HWND b : m_boxes) ::SendMessageW(b, BM_SETCHECK, BST_CHECKED, 0);
        recomputeChanged();
        notifyCb();
    }

    BEGIN_MSG_MAP(NavidromeSectionsPrefsInstance)
        MSG_WM_ERASEBKGND(OnEraseBkgnd)
        MSG_WM_CREATE(OnCreate)
        MSG_WM_SIZE(OnSize)
        COMMAND_RANGE_HANDLER_EX(IDC_FIRST, IDC_FIRST + 63, OnToggle)
    END_MSG_MAP()

    BOOL OnEraseBkgnd(CDCHandle dc) { navidrome::win::eraseLikeDialog(*this, dc); return TRUE; }

private:
    enum { IDC_HEADING = 5201, IDC_NOTE = 5202, IDC_FIRST = 5210 };

    void notifyCb() { if (m_cb.is_valid()) m_cb->on_state_changed(); }

    navidrome::CategoryKindList readHidden() const {
        navidrome::CategoryKindList hidden;
        const auto& cats = navidrome::browserCategories();
        for (std::size_t i = 0; i < cats.size() && i < m_boxes.size(); ++i)
            if (::SendMessageW(m_boxes[i], BM_GETCHECK, 0, 0) != BST_CHECKED)
                hidden.push_back(cats[i].kind);
        return hidden;
    }

    void recomputeChanged() {
        bool c = navidrome::joinHiddenCategories(readHidden()) != m_saved;
        if (c != m_changed) { m_changed = c; notifyCb(); }
    }

    LRESULT OnCreate(LPCREATESTRUCT) {
        HFONT f = navidrome::win::uiFont(*this);
        m_lineH = navidrome::win::lineHeight(*this, f);

        const auto hidden =
            navidrome::parseHiddenCategories(navidrome::cfg_browser_hidden_categories.get().c_str());
        m_saved = navidrome::joinHiddenCategories(hidden);

        m_heading = CreateWindowW(L"STATIC", L"Show these sections in the browser tree:",
            WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, *this,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_HEADING)), nullptr, nullptr);
        navidrome::win::setFont(m_heading, f);

        int id = IDC_FIRST;
        for (const auto& c : navidrome::browserCategories()) {
            HWND b = CreateWindowW(L"BUTTON", u8ToWide(c.title).c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                0, 0, 0, 0, *this, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id++)), nullptr, nullptr);
            navidrome::win::setFont(b, f);
            ::SendMessageW(b, BM_SETCHECK,
                navidrome::containsCategory(hidden, c.kind) ? BST_UNCHECKED : BST_CHECKED, 0);
            m_boxes.push_back(b);
        }

        m_note = CreateWindowW(L"STATIC",
            L"Artists (and libraries, on a multi-library server) are always shown.",
            WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, *this,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_NOTE)), nullptr, nullptr);
        navidrome::win::setFont(m_note, f);

        m_darkMode.AddDialogWithControls(*this);
        return 0;
    }

    void OnSize(UINT, CSize sz) {
        const navidrome::win::UiScale s(*this);
        const int pad = s(8), rowH = (std::max)(s(20), m_lineH + s(4));
        const int w = sz.cx - pad * 2;
        int y = pad;
        ::SetWindowPos(m_heading, nullptr, pad, y, w, m_lineH, SWP_NOZORDER);
        y += m_lineH + pad;
        for (HWND b : m_boxes) {
            ::SetWindowPos(b, nullptr, pad + s(8), y, w - s(8), rowH, SWP_NOZORDER);
            y += rowH;
        }
        y += pad;
        ::SetWindowPos(m_note, nullptr, pad, y, w, m_lineH * 2, SWP_NOZORDER);
    }

    void OnToggle(UINT, int, HWND) { recomputeChanged(); notifyCb(); }

    HWND m_heading = nullptr, m_note = nullptr;
    std::vector<HWND> m_boxes;
    int  m_lineH = 16;
    std::string m_saved;
    bool m_changed = false;
    fb2k::CCoreDarkModeHooks m_darkMode;
    preferences_page_callback::ptr m_cb;
};

class NavidromeSectionsPrefsFactory : public preferences_page_v3 {
public:
    preferences_page_instance::ptr instantiate(HWND parent,
        preferences_page_callback::ptr cb) override {
        auto inst = fb2k::service_new<NavidromeSectionsPrefsInstance>(cb);
        inst->Create(parent);
        return inst;
    }
    const char* get_name() override { return "Browser Sections"; }
    GUID        get_guid() override {
        return { 0xa1b2c3d4,0x1111,0x2222,{0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x01,0x17} };
    }
    GUID        get_parent_guid() override {
        return { 0xa1b2c3d4,0x1111,0x2222,{0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x01,0x05} };
    }
};
FB2K_SERVICE_FACTORY(NavidromeSectionsPrefsFactory);
}
