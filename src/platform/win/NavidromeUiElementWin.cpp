// Default UI panel: the Navidrome Browser as a layout element (Windows twin of
// macOS's ui_element_mac_navidrome). Each instance owns a fresh embedded
// BrowserWindow, like the Media Library prefs page; data is shared at the
// SubsonicClientWin layer, not between instances.
#include "stdafx.h"
#include "BrowserWindow.h"
#include "../../core/NavidromeDebugLog.h"
#include <SDK/ui_element.h>
#include <helpers/atl-misc.h>

namespace {

static constexpr GUID guid_ui_element_win =
    { 0xa1b2c3d4,0x1111,0x2222,{0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x01,0x14} };

class NavidromeBrowserElement : public ui_element_instance,
                                public CWindowImpl<NavidromeBrowserElement> {
public:
    DECLARE_WND_CLASS_EX(L"foo_navidrome_BrowserElem", 0, COLOR_WINDOW)

    NavidromeBrowserElement(ui_element_config::ptr cfg, ui_element_instance_callback::ptr cb)
        : m_config(cfg), m_callback(cb) {}

    void initialize_window(HWND parent) {
        // Not WIN32_OP: it lives in libPPUI, which the MSBuild project doesn't link.
        if (Create(parent, nullptr, nullptr, WS_CHILD | WS_CLIPCHILDREN) == NULL)
            throw exception_win32(GetLastError());
    }

    BEGIN_MSG_MAP_EX(NavidromeBrowserElement)
        MSG_WM_CREATE(OnCreate)
        MSG_WM_SIZE(OnSize)
        MSG_WM_ERASEBKGND(OnEraseBkgnd)
    END_MSG_MAP()

    static GUID g_get_guid() { return guid_ui_element_win; }
    static GUID g_get_subclass() { return ui_element_subclass_media_library_viewers; }
    static void g_get_name(pfc::string_base& out) { out = "Navidrome Browser"; }
    static const char* g_get_description() {
        return "Browse, search and play your Navidrome / Subsonic library.";
    }
    static ui_element_config::ptr g_get_default_configuration() {
        return ui_element_config::g_create_empty(g_get_guid());
    }

    void set_configuration(ui_element_config::ptr cfg) override { m_config = cfg; }
    ui_element_config::ptr get_configuration() override { return m_config; }
    // Colours/fonts come from ui_config_manager's callback inside BrowserWindow,
    // so nothing to do on ui_element_notify_colors_changed here.
    void notify(const GUID&, t_size, const void*, t_size) override {}

private:
    LRESULT OnCreate(LPCREATESTRUCT) {
        NAVIDROME_LOG("UI", "Default UI element instantiated");
        // In layout edit mode the right-click belongs to Default UI (replace /
        // cut / copy element): let it bubble past the browser's own menu.
        m_browser.setContextMenuPassthrough([cb = m_callback] {
            return cb.is_valid() && cb->is_edit_mode_enabled();
        });
        m_browser.createEmbedded(*this);
        return 0;
    }

    void OnSize(UINT, CSize sz) {
        if (m_browser.IsWindow())
            m_browser.SetWindowPos(nullptr, 0, 0, sz.cx, sz.cy, SWP_NOZORDER);
    }

    // The browser child covers the whole client area.
    BOOL OnEraseBkgnd(CDCHandle) { return TRUE; }

    ui_element_config::ptr                  m_config;
    const ui_element_instance_callback::ptr m_callback;
    BrowserWindow                           m_browser;
};

// ui_element_impl, not ui_element_impl_withpopup: the standalone window
// already exists (File › Open Navidrome Browser).
class NavidromeBrowserElementFactory : public ui_element_impl<NavidromeBrowserElement> {};
FB2K_SERVICE_FACTORY(NavidromeBrowserElementFactory);

} // namespace
