#pragma once

#include "../../core/LyricsPanel.h"
#include <SDK/ui_element.h>
#include <SDK/play_callback.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class LyricsWindow : public CWindowImpl<LyricsWindow>, private ui_config_callback_impl {
public:
    DECLARE_WND_CLASS_EX(L"foo_navidrome_LyricsWnd", CS_DBLCLKS, -1)

    ~LyricsWindow();

    HWND createEmbedded(HWND parent);
    void setContextMenuPassthrough(std::function<bool()> passthrough) { m_passthrough = std::move(passthrough); }

    void trackChanged(metadb_handle_ptr track);
    void playbackStopped();
    void tick();

    enum { WM_LYRICS_LOADED = WM_APP + 0x4c };

    BEGIN_MSG_MAP_EX(LyricsWindow)
        MSG_WM_CREATE(OnCreate)
        MSG_WM_DESTROY(OnDestroy)
        MSG_WM_SIZE(OnSize)
        MSG_WM_ERASEBKGND(OnEraseBkgnd)
        MSG_WM_PAINT(OnPaint)
        MSG_WM_VSCROLL(OnVScroll)
        MSG_WM_MOUSEWHEEL(OnMouseWheel)
        MSG_WM_TIMER(OnTimer)
        MSG_WM_CONTEXTMENU(OnContextMenu)
        MESSAGE_HANDLER_EX(WM_LYRICS_LOADED, OnLyricsLoaded)
    END_MSG_MAP()

private:
    struct Line {
        std::wstring text;
        int top = 0;
        int height = 0;
    };

    LRESULT OnCreate(LPCREATESTRUCT);
    void    OnDestroy();
    void    OnSize(UINT, CSize);
    BOOL    OnEraseBkgnd(CDCHandle) { return TRUE; }
    void    OnPaint(CDCHandle);
    void    OnVScroll(UINT code, UINT pos, CScrollBar);
    BOOL    OnMouseWheel(UINT, short delta, CPoint);
    void    OnTimer(UINT_PTR id);
    void    OnContextMenu(CWindow, CPoint pt);
    LRESULT OnLyricsLoaded(UINT, WPARAM, LPARAM lp);

    void ui_fonts_changed() override;
    void ui_colors_changed() override;

    void refreshTheme();
    void rebuildFonts();
    void showStatus(navidrome::LyricsPanelStatus status);
    void layoutLines();
    void updateScrollBar();
    void scrollTo(int y);
    void centerActiveLine();
    void copyLyricsToClipboard();

    std::unique_ptr<play_callback_impl_base> m_callback;
    std::function<bool()> m_passthrough;

    std::string                   m_uri;
    navidrome::Lyrics             m_lyrics;
    navidrome::LyricsPanelStatus  m_status = navidrome::LyricsPanelStatus::NothingPlaying;
    std::uint64_t                 m_generation = 0;
    std::vector<Line>             m_lines;
    int                           m_activeLine = -1;
    int                           m_contentHeight = 0;
    int                           m_scrollY = 0;
    unsigned long long            m_userScrollUntil = 0;

    HFONT    m_font = nullptr;
    HFONT    m_boldFont = nullptr;
    int      m_lineHeight = 16;
    COLORREF m_text = 0;
    COLORREF m_dim = 0;
    COLORREF m_bg = 0;
};
