#include "stdafx.h"
#include "LyricsWindow.h"
#include "../../core/NavidromeDebugLog.h"
#include "../../core/NavidromeGuids.h"
#include <SDK/ui_element.h>
#include <helpers/atl-misc.h>
#include "../../../third_party/columns_ui-sdk/ui_extension.h"

namespace {

constexpr const char* kLyricsName = "Navidrome Lyrics";
constexpr const char* kLyricsDescription = "Lyrics of the playing Navidrome track, with the current line highlighted.";

class LyricsHost : public CWindowImpl<LyricsHost> {
public:
    DECLARE_WND_CLASS_EX(L"foo_navidrome_LyricsHost", 0, COLOR_WINDOW)

    void setContextMenuPassthrough(std::function<bool()> passthrough) {
        m_lyrics.setContextMenuPassthrough(std::move(passthrough));
    }

    BEGIN_MSG_MAP_EX(LyricsHost)
        MSG_WM_CREATE(OnCreate)
        MSG_WM_SIZE(OnSize)
        MSG_WM_ERASEBKGND(OnEraseBkgnd)
    END_MSG_MAP()

private:
    LRESULT OnCreate(LPCREATESTRUCT) {
        m_lyrics.createEmbedded(*this);
        return 0;
    }

    void OnSize(UINT, CSize sz) {
        if (m_lyrics.IsWindow()) m_lyrics.SetWindowPos(nullptr, 0, 0, sz.cx, sz.cy, SWP_NOZORDER);
    }

    BOOL OnEraseBkgnd(CDCHandle) { return TRUE; }

    LyricsWindow m_lyrics;
};

class NavidromeLyricsElement : public ui_element_instance, public LyricsHost {
public:
    NavidromeLyricsElement(ui_element_config::ptr cfg, ui_element_instance_callback::ptr cb)
        : m_config(cfg), m_callback(cb) {}

    void initialize_window(HWND parent) {
        setContextMenuPassthrough([cb = m_callback] { return cb.is_valid() && cb->is_edit_mode_enabled(); });
        if (Create(parent, nullptr, nullptr, WS_CHILD | WS_CLIPCHILDREN) == NULL)
            throw exception_win32(GetLastError());
        NAVIDROME_LOG("UI", "Default UI lyrics element instantiated");
    }

    static GUID g_get_guid() { return navidrome::guids::winUiElementLyrics; }
    static GUID g_get_subclass() { return ui_element_subclass_playback_information; }
    static void g_get_name(pfc::string_base& out) { out = kLyricsName; }
    static const char* g_get_description() { return kLyricsDescription; }
    static ui_element_config::ptr g_get_default_configuration() {
        return ui_element_config::g_create_empty(g_get_guid());
    }

    void set_configuration(ui_element_config::ptr cfg) override { m_config = cfg; }
    ui_element_config::ptr get_configuration() override { return m_config; }
    void notify(const GUID&, t_size, const void*, t_size) override {}

private:
    ui_element_config::ptr                  m_config;
    const ui_element_instance_callback::ptr m_callback;
};

class NavidromeLyricsElementFactory : public ui_element_impl<NavidromeLyricsElement> {};
FB2K_SERVICE_FACTORY(NavidromeLyricsElementFactory);

class NavidromeLyricsCuiPanel : public uie::window {
public:
    const GUID& get_extension_guid() const override { return navidrome::guids::winColumnsUiLyrics; }
    void get_name(pfc::string_base& out) const override { out = kLyricsName; }
    void get_category(pfc::string_base& out) const override { out = "Panels"; }
    bool get_short_name(pfc::string_base& out) const override { out = "Lyrics"; return true; }
    bool get_description(pfc::string_base& out) const override { out = kLyricsDescription; return true; }
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
            NAVIDROME_ERR("UI", "Columns UI lyrics panel creation failed: " + std::to_string(GetLastError()));
            m_host.release();
            return nullptr;
        }
        NAVIDROME_LOG("UI", "Columns UI lyrics panel instantiated");
        return m_wnd;
    }

    void destroy_window() override {
        if (m_wnd.IsWindow()) m_wnd.DestroyWindow();
        m_host.release();
    }

    HWND get_wnd() const override { return m_wnd.m_hWnd; }

private:
    LyricsHost           m_wnd;
    uie::window_host_ptr m_host;
};

uie::window_factory<NavidromeLyricsCuiPanel> g_navidrome_cui_lyrics;

}
