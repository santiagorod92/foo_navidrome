#include "stdafx.h"
#include "BrowserWindow.h"
#include "../../core/NavidromeDebugLog.h"
#include "../../../third_party/columns_ui-sdk/ui_extension.h"

namespace {

static constexpr GUID guid_cui_panel_win =
    { 0xa1b2c3d4,0x1111,0x2222,{0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x01,0x15} };

class NavidromeBrowserCuiHost : public CWindowImpl<NavidromeBrowserCuiHost> {
public:
    DECLARE_WND_CLASS_EX(L"foo_navidrome_BrowserCui", 0, COLOR_WINDOW)

    BEGIN_MSG_MAP_EX(NavidromeBrowserCuiHost)
        MSG_WM_CREATE(OnCreate)
        MSG_WM_SIZE(OnSize)
        MSG_WM_ERASEBKGND(OnEraseBkgnd)
    END_MSG_MAP()

private:
    LRESULT OnCreate(LPCREATESTRUCT) {
        NAVIDROME_LOG("UI", "Columns UI panel instantiated");
        m_browser.createEmbedded(*this);
        return 0;
    }

    void OnSize(UINT, CSize sz) {
        if (m_browser.IsWindow())
            m_browser.SetWindowPos(nullptr, 0, 0, sz.cx, sz.cy, SWP_NOZORDER);
    }

    BOOL OnEraseBkgnd(CDCHandle) { return TRUE; }

    BrowserWindow m_browser;
};

class NavidromeBrowserCuiPanel : public uie::window {
public:
    const GUID& get_extension_guid() const override { return guid_cui_panel_win; }
    void get_name(pfc::string_base& out) const override { out = "Navidrome Browser"; }
    void get_category(pfc::string_base& out) const override { out = "Panels"; }
    bool get_short_name(pfc::string_base& out) const override { out = "Navidrome"; return true; }
    bool get_description(pfc::string_base& out) const override {
        out = "Browse, search and play your Navidrome / Subsonic library.";
        return true;
    }
    unsigned get_type() const override { return uie::type_panel; }
    bool is_available(const uie::window_host_ptr&) const override { return true; }

    HWND create_or_transfer_window(HWND parent, const uie::window_host_ptr& host,
                                   const ui_helpers::window_position_t& pos) override {
        if (m_wnd.IsWindow()) {
            m_wnd.ShowWindow(SW_HIDE);
            m_wnd.SetParent(parent);
            m_host->relinquish_ownership(m_wnd);
            m_host = host;
            m_wnd.SetWindowPos(nullptr, pos.x, pos.y, pos.cx, pos.cy, SWP_NOZORDER);
            return m_wnd;
        }
        m_host = host;
        RECT rc{ pos.x, pos.y, pos.x + static_cast<int>(pos.cx), pos.y + static_cast<int>(pos.cy) };
        if (m_wnd.Create(parent, rc, nullptr, WS_CHILD | WS_CLIPCHILDREN, WS_EX_CONTROLPARENT, 0U) == NULL) {
            NAVIDROME_ERR("UI", "Columns UI panel window creation failed: " + std::to_string(GetLastError()));
            m_host.release();
            return nullptr;
        }
        return m_wnd;
    }

    void destroy_window() override {
        if (m_wnd.IsWindow()) m_wnd.DestroyWindow();
        m_host.release();
    }

    HWND get_wnd() const override { return m_wnd.m_hWnd; }

private:
    NavidromeBrowserCuiHost m_wnd;
    uie::window_host_ptr    m_host;
};

uie::window_factory<NavidromeBrowserCuiPanel> g_navidrome_cui_panel;
}
