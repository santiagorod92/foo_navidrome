#include "stdafx.h"
#include "LyricsWindow.h"
#include "WinUi.h"
#include "../../core/NavidromeBrowserModel.h"
#include "../../core/NavidromeLibraryPlatform.h"
#include "../../core/NavidromeDebugLog.h"
#include <SDK/playback_control.h>
#include <uxtheme.h>
#include <algorithm>
#include <thread>

namespace {

constexpr UINT_PTR kTickTimer = 1;
constexpr UINT     kTickMs = 200;
constexpr unsigned long long kUserScrollHoldMs = 4000;
constexpr UINT     kCmdCopy = 1;

struct LoadedLyrics {
    std::uint64_t     generation = 0;
    navidrome::Lyrics lyrics;
    std::string       error;
};

class LyricsPlayCallback : public play_callback_impl_base {
public:
    explicit LyricsPlayCallback(LyricsWindow& owner)
        : play_callback_impl_base(flag_on_playback_new_track | flag_on_playback_stop | flag_on_playback_seek),
          m_owner(owner) {}

    void on_playback_new_track(metadb_handle_ptr track) override { m_owner.trackChanged(track); }
    void on_playback_stop(play_control::t_stop_reason reason) override {
        if (reason != play_control::stop_reason_starting_another) m_owner.playbackStopped();
    }
    void on_playback_seek(double) override { m_owner.tick(); }

private:
    LyricsWindow& m_owner;
};

COLORREF blend(COLORREF a, COLORREF b, int percentA) {
    auto mix = [&](int x, int y) { return (x * percentA + y * (100 - percentA)) / 100; };
    return RGB(mix(GetRValue(a), GetRValue(b)), mix(GetGValue(a), GetGValue(b)), mix(GetBValue(a), GetBValue(b)));
}

const UINT kTextFlags = DT_CENTER | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL;

}

LyricsWindow::~LyricsWindow() {
    if (m_font) ::DeleteObject(m_font);
    if (m_boldFont) ::DeleteObject(m_boldFont);
}

HWND LyricsWindow::createEmbedded(HWND parent) {
    return Create(parent, nullptr, nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN);
}

LRESULT LyricsWindow::OnCreate(LPCREATESTRUCT) {
    rebuildFonts();
    refreshTheme();
    m_callback = std::make_unique<LyricsPlayCallback>(*this);
    SetTimer(kTickTimer, kTickMs);
    NAVIDROME_LOG("Lyrics", "Windows lyrics panel created");
    metadb_handle_ptr track;
    if (playback_control::get()->get_now_playing(track)) trackChanged(track);
    else showStatus(navidrome::LyricsPanelStatus::NothingPlaying);
    return 0;
}

void LyricsWindow::OnDestroy() {
    KillTimer(kTickTimer);
    m_callback.reset();
    ++m_generation;
}

void LyricsWindow::trackChanged(metadb_handle_ptr track) {
    const std::string uri = track.is_valid() ? std::string(track->get_path()) : std::string();
    if (uri == m_uri) return;
    m_uri = uri;
    const std::uint64_t generation = ++m_generation;
    m_lyrics = {};

    const auto status = navidrome::lyricsPanelStatusForTrack(uri, navidrome::libraryCore().isConfigured());
    showStatus(status);
    if (status != navidrome::LyricsPanelStatus::Loading) return;

    const HWND wnd = m_hWnd;
    std::thread([wnd, generation, uri]() {
        auto* payload = new LoadedLyrics{};
        payload->generation = generation;
        navidrome::dbg::runGuarded("Lyrics", "panel fetch", [&] {
            payload->lyrics = navidrome::lyricsForTrackURI(navidrome::libraryClient(), uri, payload->error);
        });
        if (!payload->error.empty()) NAVIDROME_WARN("Lyrics", "panel fetch failed: " + payload->error);
        if (!::PostMessage(wnd, WM_LYRICS_LOADED, 0, reinterpret_cast<LPARAM>(payload))) delete payload;
    }).detach();
}

void LyricsWindow::playbackStopped() {
    m_uri.clear();
    ++m_generation;
    m_lyrics = {};
    showStatus(navidrome::LyricsPanelStatus::NothingPlaying);
}

LRESULT LyricsWindow::OnLyricsLoaded(UINT, WPARAM, LPARAM lp) {
    std::unique_ptr<LoadedLyrics> payload(reinterpret_cast<LoadedLyrics*>(lp));
    if (payload->generation != m_generation) return 0;
    const auto status = navidrome::lyricsPanelStatusForResult(payload->lyrics, payload->error);
    NAVIDROME_LOG("Lyrics", "panel shows " + std::to_string(payload->lyrics.lines.size()) + " line(s), " +
                  (payload->lyrics.synced ? "synced" : "unsynced"));
    if (status != navidrome::LyricsPanelStatus::Showing) {
        showStatus(status);
        return 0;
    }
    m_lyrics = std::move(payload->lyrics);
    m_status = status;
    m_activeLine = -1;
    m_userScrollUntil = 0;
    layoutLines();
    m_scrollY = 0;
    updateScrollBar();
    tick();
    Invalidate();
    return 0;
}

void LyricsWindow::showStatus(navidrome::LyricsPanelStatus status) {
    m_status = status;
    m_lines.clear();
    m_activeLine = -1;
    m_contentHeight = 0;
    m_scrollY = 0;
    if (IsWindow()) {
        updateScrollBar();
        Invalidate();
    }
}

void LyricsWindow::tick() {
    if (m_status != navidrome::LyricsPanelStatus::Showing || !m_lyrics.synced || m_lines.empty()) return;
    const double pos = playback_control::get()->playback_get_position();
    const int line = navidrome::activeLyricLine(m_lyrics, static_cast<long long>(pos * 1000.0));
    if (line == m_activeLine) return;
    m_activeLine = line;
    if (::GetTickCount64() >= m_userScrollUntil) centerActiveLine();
    Invalidate();
}

void LyricsWindow::layoutLines() {
    m_lines.clear();
    m_contentHeight = 0;
    if (m_status != navidrome::LyricsPanelStatus::Showing || !IsWindow()) return;

    const navidrome::win::UiScale s(*this);
    CRect rc;
    GetClientRect(&rc);
    const int pad = s(12);
    const int width = (std::max)(s(40), rc.Width() - 2 * pad);
    CClientDC dc(*this);
    const HFONT old = dc.SelectFont(m_boldFont ? m_boldFont : m_font);
    int y = s(16);
    for (const auto& l : m_lyrics.lines) {
        Line line;
        line.text = navidrome::win::u8ToWide(l.text);
        CRect measure(0, 0, width, 0);
        const std::wstring probe = line.text.empty() ? std::wstring(L" ") : line.text;
        dc.DrawText(probe.c_str(), -1, &measure, kTextFlags | DT_CALCRECT);
        line.top = y;
        line.height = (std::max)(static_cast<int>(measure.Height()), m_lineHeight);
        y += line.height + s(6);
        m_lines.push_back(std::move(line));
    }
    dc.SelectFont(old);
    m_contentHeight = y + s(16);
}

void LyricsWindow::updateScrollBar() {
    CRect rc;
    GetClientRect(&rc);
    const int maxScroll = (std::max)(0, m_contentHeight - rc.Height());
    m_scrollY = (std::min)(m_scrollY, maxScroll);
    SCROLLINFO si{ sizeof(si) };
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = m_contentHeight > rc.Height() ? m_contentHeight - 1 : 0;
    si.nPage = m_contentHeight > rc.Height() ? static_cast<UINT>(rc.Height()) : 0;
    si.nPos = m_scrollY;
    SetScrollInfo(SB_VERT, &si, TRUE);
}

void LyricsWindow::scrollTo(int y) {
    CRect rc;
    GetClientRect(&rc);
    y = (std::max)(0, (std::min)(y, (std::max)(0, m_contentHeight - rc.Height())));
    if (y == m_scrollY) return;
    m_scrollY = y;
    SetScrollPos(SB_VERT, m_scrollY, TRUE);
    Invalidate();
}

void LyricsWindow::centerActiveLine() {
    if (m_activeLine < 0 || static_cast<size_t>(m_activeLine) >= m_lines.size()) return;
    CRect rc;
    GetClientRect(&rc);
    const Line& l = m_lines[m_activeLine];
    scrollTo(navidrome::lyricsScrollToCenter(l.top, l.height, rc.Height(), m_contentHeight));
}

void LyricsWindow::OnSize(UINT, CSize) {
    if (m_status == navidrome::LyricsPanelStatus::Showing) layoutLines();
    updateScrollBar();
    if (::GetTickCount64() >= m_userScrollUntil) centerActiveLine();
    Invalidate();
}

void LyricsWindow::OnPaint(CDCHandle) {
    CPaintDC paint(*this);
    CRect rc;
    GetClientRect(&rc);
    CDC mem;
    mem.CreateCompatibleDC(paint);
    CBitmap bmp;
    bmp.CreateCompatibleBitmap(paint, (std::max)(1, rc.Width()), (std::max)(1, rc.Height()));
    const HBITMAP oldBmp = mem.SelectBitmap(bmp);
    mem.FillSolidRect(&rc, m_bg);
    mem.SetBkMode(TRANSPARENT);

    const navidrome::win::UiScale s(*this);
    const int pad = s(12);
    const HFONT oldFont = mem.SelectFont(m_font);

    if (m_status == navidrome::LyricsPanelStatus::Showing) {
        for (size_t i = 0; i < m_lines.size(); ++i) {
            const Line& l = m_lines[i];
            const int top = l.top - m_scrollY;
            if (top + l.height < 0) continue;
            if (top > rc.Height()) break;
            const bool active = m_lyrics.synced && static_cast<int>(i) == m_activeLine;
            mem.SelectFont(active && m_boldFont ? m_boldFont : m_font);
            mem.SetTextColor(!m_lyrics.synced || active ? m_text : m_dim);
            CRect lineRc(pad, top, rc.Width() - pad, top + l.height);
            mem.DrawText(l.text.c_str(), -1, &lineRc, kTextFlags);
        }
    } else {
        const std::wstring msg = navidrome::win::u8ToWide(navidrome::lyricsPanelMessage(m_status));
        mem.SetTextColor(m_dim);
        CRect msgRc(pad, s(16), rc.Width() - pad, rc.Height());
        mem.DrawText(msg.c_str(), -1, &msgRc, kTextFlags);
    }

    mem.SelectFont(oldFont);
    paint.BitBlt(0, 0, rc.Width(), rc.Height(), mem, 0, 0, SRCCOPY);
    mem.SelectBitmap(oldBmp);
}

void LyricsWindow::OnVScroll(UINT code, UINT, CScrollBar) {
    CRect rc;
    GetClientRect(&rc);
    SCROLLINFO si{ sizeof(si) };
    si.fMask = SIF_ALL;
    GetScrollInfo(SB_VERT, &si);
    int y = m_scrollY;
    switch (code) {
        case SB_LINEUP:        y -= m_lineHeight; break;
        case SB_LINEDOWN:      y += m_lineHeight; break;
        case SB_PAGEUP:        y -= rc.Height() * 9 / 10; break;
        case SB_PAGEDOWN:      y += rc.Height() * 9 / 10; break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: y = si.nTrackPos; break;
        case SB_TOP:           y = 0; break;
        case SB_BOTTOM:        y = m_contentHeight; break;
        default: return;
    }
    m_userScrollUntil = ::GetTickCount64() + kUserScrollHoldMs;
    scrollTo(y);
}

BOOL LyricsWindow::OnMouseWheel(UINT, short delta, CPoint) {
    if (m_contentHeight == 0) return TRUE;
    m_userScrollUntil = ::GetTickCount64() + kUserScrollHoldMs;
    scrollTo(m_scrollY - delta * 3 * m_lineHeight / WHEEL_DELTA);
    return TRUE;
}

void LyricsWindow::OnTimer(UINT_PTR id) {
    if (id == kTickTimer) tick();
}

void LyricsWindow::OnContextMenu(CWindow, CPoint pt) {
    if (m_passthrough && m_passthrough()) {
        SetMsgHandled(FALSE);
        return;
    }
    if (pt.x == -1 && pt.y == -1) {
        pt = CPoint(0, 0);
        ClientToScreen(&pt);
    }
    CMenu menu;
    menu.CreatePopupMenu();
    const bool canCopy = m_status == navidrome::LyricsPanelStatus::Showing;
    menu.AppendMenu(MF_STRING | (canCopy ? 0 : MF_GRAYED), kCmdCopy, L"Copy lyrics");
    const UINT cmd = menu.TrackPopupMenu(TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, pt.x, pt.y, *this);
    if (cmd == kCmdCopy) copyLyricsToClipboard();
}

void LyricsWindow::copyLyricsToClipboard() {
    std::wstring text;
    for (size_t i = 0; i < m_lines.size(); ++i) {
        if (i) text += L"\r\n";
        text += m_lines[i].text;
    }
    if (!OpenClipboard()) {
        NAVIDROME_WARN("Lyrics", "copy: clipboard busy");
        return;
    }
    ::EmptyClipboard();
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    if (HGLOBAL mem = ::GlobalAlloc(GMEM_MOVEABLE, bytes)) {
        memcpy(::GlobalLock(mem), text.c_str(), bytes);
        ::GlobalUnlock(mem);
        if (!::SetClipboardData(CF_UNICODETEXT, mem)) {
            ::GlobalFree(mem);
            NAVIDROME_WARN("Lyrics", "copy: SetClipboardData failed");
        }
    }
    ::CloseClipboard();
}

void LyricsWindow::rebuildFonts() {
    if (m_font) { ::DeleteObject(m_font); m_font = nullptr; }
    if (m_boldFont) { ::DeleteObject(m_boldFont); m_boldFont = nullptr; }

    HFONT base = nullptr;
    if (auto cfg = ui_config_manager::tryGet(); cfg.is_valid()) base = cfg->query_font(ui_font_default);
    if (!base) base = navidrome::win::uiFont(*this);
    LOGFONT lf{};
    ::GetObject(base, sizeof(lf), &lf);
    m_font = ::CreateFontIndirect(&lf);
    lf.lfWeight = FW_BOLD;
    m_boldFont = ::CreateFontIndirect(&lf);
    m_lineHeight = navidrome::win::lineHeight(*this, m_font);
}

void LyricsWindow::refreshTheme() {
    m_text = ::GetSysColor(COLOR_WINDOWTEXT);
    m_bg = ::GetSysColor(COLOR_WINDOW);
    bool dark = false;
    if (auto cfg = ui_config_manager::tryGet(); cfg.is_valid()) {
        t_ui_color c = 0;
        if (cfg->query_color(ui_color_text, c)) m_text = static_cast<COLORREF>(c);
        if (cfg->query_color(ui_color_background, c)) m_bg = static_cast<COLORREF>(c);
        dark = cfg->is_dark_mode();
    }
    m_dim = blend(m_text, m_bg, 55);
    ::SetWindowTheme(*this, dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
    Invalidate();
}

void LyricsWindow::ui_fonts_changed() {
    rebuildFonts();
    layoutLines();
    updateScrollBar();
    Invalidate();
}

void LyricsWindow::ui_colors_changed() {
    refreshTheme();
}
