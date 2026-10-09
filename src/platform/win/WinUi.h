#pragma once
#include <windows.h>
#include <string>

namespace navidrome::win {

inline std::wstring u8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n, 0);
    ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    if (!w.empty() && w.back() == 0) w.pop_back();
    return w;
}

inline std::string wToU8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
    if (!s.empty() && s.back() == 0) s.pop_back();
    return s;
}

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

inline HFONT uiFont(HWND wnd) {
    for (HWND w = wnd; w; w = ::GetParent(w)) {
        if (auto f = reinterpret_cast<HFONT>(::SendMessageW(w, WM_GETFONT, 0, 0))) return f;
    }
    return reinterpret_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT));
}

inline void setFont(HWND ctrl, HFONT font) {
    ::SendMessageW(ctrl, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
}

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

inline int fitWidth(HWND ctrl, const UiScale& s, int chrome96, int min96 = 0) {
    int w = textWidth(ctrl) + s(chrome96);
    return w > s(min96) ? w : s(min96);
}

inline void eraseLikeDialog(HWND wnd, HDC dc) {
    RECT rc{};
    ::GetClientRect(wnd, &rc);
    auto brush = reinterpret_cast<HBRUSH>(::SendMessageW(wnd, WM_CTLCOLORDLG,
        reinterpret_cast<WPARAM>(dc), reinterpret_cast<LPARAM>(wnd)));
    ::FillRect(dc, &rc, brush ? brush : ::GetSysColorBrush(COLOR_BTNFACE));
}
}
