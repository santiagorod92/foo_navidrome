#pragma once
// Layout helpers for the component's hand-built Win32 windows (no .rc dialogs,
// so nothing scales for us). Coordinates are written at 96 DPI and passed
// through UiScale; widths that hold text are measured, not guessed, since the
// font depends on the host dialog and on the system (issue #18: high-DPI
// Windows clipped every label and button).
#include <windows.h>
#include <string>

namespace navidrome::win {

// The window's DPI (per-monitor when foobar runs per-monitor aware), falling
// back to the system DPI on Windows versions without GetDpiForWindow.
inline UINT dpiOf(HWND wnd) {
    using GetDpiForWindowFn = UINT (WINAPI*)(HWND);
    static const auto getDpiForWindow = reinterpret_cast<GetDpiForWindowFn>(
        ::GetProcAddress(::GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    if (getDpiForWindow && wnd) {
        if (UINT dpi = getDpiForWindow(wnd)) return dpi;
    }
    HDC dc = ::GetDC(nullptr);
    int dpi = dc ? ::GetDeviceCaps(dc, LOGPIXELSY) : 96;
    if (dc) ::ReleaseDC(nullptr, dc);
    return dpi > 0 ? static_cast<UINT>(dpi) : 96;
}

struct UiScale {
    UINT dpi = 96;
    UiScale() = default;
    explicit UiScale(HWND wnd) : dpi(dpiOf(wnd)) {}
    int operator()(int px96) const { return ::MulDiv(px96, static_cast<int>(dpi), 96); }
};

// The font of the nearest ancestor that has one (the Preferences dialog for a
// prefs page, so our controls match foobar's own pages), else the stock GUI
// font. Not owned by the caller.
inline HFONT uiFont(HWND wnd) {
    for (HWND w = wnd; w; w = ::GetParent(w)) {
        if (auto f = reinterpret_cast<HFONT>(::SendMessageW(w, WM_GETFONT, 0, 0))) return f;
    }
    return reinterpret_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT));
}

inline void setFont(HWND ctrl, HFONT font) {
    ::SendMessageW(ctrl, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
}

// Pixel width of a control's current text in its own font (single line).
inline int textWidth(HWND ctrl) {
    int len = ::GetWindowTextLengthW(ctrl);
    if (len <= 0) return 0;
    std::wstring text(static_cast<std::size_t>(len) + 1, L'\0');
    ::GetWindowTextW(ctrl, &text[0], len + 1);
    HDC dc = ::GetDC(ctrl);
    if (!dc) return 0;
    auto font = reinterpret_cast<HFONT>(::SendMessageW(ctrl, WM_GETFONT, 0, 0));
    HGDIOBJ old = font ? ::SelectObject(dc, font) : nullptr;
    SIZE sz{};
    ::GetTextExtentPoint32W(dc, text.c_str(), len, &sz);
    if (old) ::SelectObject(dc, old);
    ::ReleaseDC(ctrl, dc);
    return sz.cx;
}

// Height of one line of text in `font`.
inline int lineHeight(HWND wnd, HFONT font) {
    HDC dc = ::GetDC(wnd);
    if (!dc) return 16;
    HGDIOBJ old = ::SelectObject(dc, font);
    TEXTMETRICW tm{};
    ::GetTextMetricsW(dc, &tm);
    ::SelectObject(dc, old);
    ::ReleaseDC(wnd, dc);
    return tm.tmHeight;
}

// Width for a push button / checkbox so its caption never clips: measured text
// plus `chrome96` (button padding, or the check box glyph), at least `min96`.
inline int fitWidth(HWND ctrl, const UiScale& s, int chrome96, int min96 = 0) {
    int w = textWidth(ctrl) + s(chrome96);
    return w > s(min96) ? w : s(min96);
}

// Paint a hand-built window's background the way a dialog does: ask for WM_CTLCOLORDLG.
// The Dark Mode hooks (CCoreDarkModeHooks::AddDialog) answer it with the dark brush; otherwise
// DefWindowProc's default brush comes back. Without this a CWindowImpl page keeps its white class
// brush under dark controls (issue #18, seen on real Windows).
inline void eraseLikeDialog(HWND wnd, HDC dc) {
    RECT rc{};
    ::GetClientRect(wnd, &rc);
    auto brush = reinterpret_cast<HBRUSH>(::SendMessageW(wnd, WM_CTLCOLORDLG,
        reinterpret_cast<WPARAM>(dc), reinterpret_cast<LPARAM>(wnd)));
    ::FillRect(dc, &rc, brush ? brush : ::GetSysColorBrush(COLOR_BTNFACE));
}

} // namespace navidrome::win
