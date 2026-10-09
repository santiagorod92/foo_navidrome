#include "stdafx.h"
#include "BrowserPrompts.h"
#include "WinUi.h"
#include <uxtheme.h>
#include <commctrl.h>
#include <shlobj.h>
#include <string>

using navidrome::win::u8ToWide;
using navidrome::win::wToU8;

namespace {

void placePrompt(CWindow& w, HWND owner, SIZE client) {
    RECT rc{ 0, 0, client.cx, client.cy };
    ::AdjustWindowRectEx(&rc, static_cast<DWORD>(w.GetWindowLongPtr(GWL_STYLE)), FALSE,
                         static_cast<DWORD>(w.GetWindowLongPtr(GWL_EXSTYLE)));
    const int ww = rc.right - rc.left, wh = rc.bottom - rc.top;
    RECT rcOwner{};
    if (owner && ::GetWindowRect(owner, &rcOwner)) {
        int x = rcOwner.left + ((rcOwner.right - rcOwner.left) - ww) / 2;
        int y = rcOwner.top + ((rcOwner.bottom - rcOwner.top) - wh) / 2;
        w.SetWindowPos(nullptr, x, y, ww, wh, SWP_NOZORDER);
    } else {
        w.SetWindowPos(nullptr, 0, 0, ww, wh, SWP_NOMOVE | SWP_NOZORDER);
    }
}

SIZE layoutOkCancel(HWND parent, HFONT f, const navidrome::win::UiScale& s,
                    int right, int y, int btnH) {
    using namespace navidrome::win;
    auto mk = [&](const wchar_t* text, DWORD style, int id) {
        HWND b = CreateWindowW(L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | style,
            0, 0, 0, 0, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
        setFont(b, f);
        return b;
    };
    HWND ok = mk(L"OK", BS_DEFPUSHBUTTON, IDOK);
    HWND cancel = mk(L"Cancel", BS_PUSHBUTTON, IDCANCEL);
    const int okW = fitWidth(ok, s, 24, 76), cancelW = fitWidth(cancel, s, 24, 76);
    ::SetWindowPos(cancel, nullptr, right - cancelW, y, cancelW, btnH, SWP_NOZORDER);
    ::SetWindowPos(ok, nullptr, right - cancelW - s(8) - okW, y, okW, btnH, SWP_NOZORDER);
    return SIZE{ right + s(12), y + btnH + s(12) };
}

class TextPromptWindow : public CWindowImpl<TextPromptWindow> {
public:
    DECLARE_WND_CLASS(L"foo_navidrome_PromptWnd")

    static bool run(HWND owner, const wchar_t* title, const wchar_t* label,
                    const std::wstring& initial, std::wstring& out) {
        TextPromptWindow w;
        w.m_label = label;
        w.m_value = initial;

        w.Create(owner, CWindow::rcDefault, title,
                 WS_POPUP | WS_CAPTION | WS_SYSMENU, WS_EX_DLGMODALFRAME);
        if (!w.IsWindow()) return false;

        placePrompt(w, owner, w.m_clientSize);

        if (owner) ::EnableWindow(owner, FALSE);
        w.ShowWindow(SW_SHOW);
        w.m_edit.SetFocus();

        MSG msg;
        while (!w.m_done) {
            BOOL got = ::GetMessageW(&msg, nullptr, 0, 0);
            if (got == 0) {
                ::PostQuitMessage(static_cast<int>(msg.wParam));
                break;
            }
            if (got == -1) break;
            if (!::IsDialogMessageW(w.m_hWnd, &msg)) {
                ::TranslateMessage(&msg);
                ::DispatchMessageW(&msg);
            }
        }
        if (owner) { ::EnableWindow(owner, TRUE); ::SetForegroundWindow(owner); }
        if (w.IsWindow()) w.DestroyWindow();

        out = w.m_value;
        return w.m_accepted;
    }

    BEGIN_MSG_MAP(TextPromptWindow)
        MSG_WM_ERASEBKGND(OnEraseBkgnd)
        MSG_WM_CREATE(OnCreate)
        MSG_WM_CLOSE(OnClose)
        COMMAND_ID_HANDLER_EX(IDOK,     OnOk)
        COMMAND_ID_HANDLER_EX(IDCANCEL, OnCancel)
    END_MSG_MAP()

    BOOL OnEraseBkgnd(CDCHandle dc) { navidrome::win::eraseLikeDialog(*this, dc); return TRUE; }

private:
    enum { IDC_PROMPT_LABEL = 4001, IDC_PROMPT_EDIT = 4002 };

    LRESULT OnCreate(LPCREATESTRUCT) {
        using namespace navidrome::win;
        const UiScale s(*this);
        const HFONT f = uiFont(*this);
        const int pad = s(12), fieldW = s(330), lineH = lineHeight(*this, f);
        const int rowH = (std::max)(s(22), lineH + s(8));

        HWND label = CreateWindowW(L"STATIC", m_label.c_str(), WS_CHILD | WS_VISIBLE,
            pad, pad, fieldW, lineH, *this,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PROMPT_LABEL)), nullptr, nullptr);
        setFont(label, f);

        int y = pad + lineH + s(4);
        m_edit.Create(*this, CWindow::rcDefault, nullptr,
            WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL,
            0, IDC_PROMPT_EDIT);
        m_edit.SetWindowPos(nullptr, pad, y, fieldW, rowH, SWP_NOZORDER);
        m_edit.SetFont(f);
        m_edit.SetWindowText(m_value.c_str());
        m_edit.SetSel(0, -1);
        y += rowH + s(12);

        m_clientSize = layoutOkCancel(*this, f, s, pad + fieldW, y, rowH + s(4));
        m_darkMode.AddDialogWithControls(*this);
        return 0;
    }

    void OnOk(UINT, int, HWND) {
        int len = m_edit.GetWindowTextLength();
        std::wstring buf(static_cast<std::size_t>(len) + 1, L'\0');
        m_edit.GetWindowText(&buf[0], len + 1);
        buf.resize(static_cast<std::size_t>(len));
        m_value    = buf;
        m_accepted = true;
        m_done     = true;
    }

    void OnCancel(UINT, int, HWND) { m_done = true; }
    void OnClose()                 { m_done = true; }

    CEdit        m_edit;
    std::wstring m_label;
    std::wstring m_value;
    SIZE         m_clientSize{};
    bool         m_accepted = false;
    bool         m_done     = false;
    fb2k::CCoreDarkModeHooks m_darkMode;
};

class RadioStationPromptWindow : public CWindowImpl<RadioStationPromptWindow> {
public:
    DECLARE_WND_CLASS(L"foo_navidrome_RadioPromptWnd")

    static bool run(HWND owner, const wchar_t* title,
                    const std::wstring& initialName,
                    const std::wstring& initialStreamURL,
                    const std::wstring& initialHomePageURL,
                    std::wstring& outName, std::wstring& outStreamURL,
                    std::wstring& outHomePageURL) {
        RadioStationPromptWindow w;
        w.m_name        = initialName;
        w.m_streamURL   = initialStreamURL;
        w.m_homePageURL = initialHomePageURL;

        w.Create(owner, CWindow::rcDefault, title,
                 WS_POPUP | WS_CAPTION | WS_SYSMENU, WS_EX_DLGMODALFRAME);
        if (!w.IsWindow()) return false;

        placePrompt(w, owner, w.m_clientSize);

        if (owner) ::EnableWindow(owner, FALSE);
        w.ShowWindow(SW_SHOW);
        w.m_nameEdit.SetFocus();

        MSG msg;
        while (!w.m_done) {
            BOOL got = ::GetMessageW(&msg, nullptr, 0, 0);
            if (got == 0) {
                ::PostQuitMessage(static_cast<int>(msg.wParam));
                break;
            }
            if (got == -1) break;
            if (!::IsDialogMessageW(w.m_hWnd, &msg)) {
                ::TranslateMessage(&msg);
                ::DispatchMessageW(&msg);
            }
        }
        if (owner) { ::EnableWindow(owner, TRUE); ::SetForegroundWindow(owner); }
        if (w.IsWindow()) w.DestroyWindow();

        outName        = w.m_name;
        outStreamURL   = w.m_streamURL;
        outHomePageURL = w.m_homePageURL;
        return w.m_accepted;
    }

    BEGIN_MSG_MAP(RadioStationPromptWindow)
        MSG_WM_ERASEBKGND(OnEraseBkgnd)
        MSG_WM_CREATE(OnCreate)
        MSG_WM_CLOSE(OnClose)
        COMMAND_ID_HANDLER_EX(IDOK,     OnOk)
        COMMAND_ID_HANDLER_EX(IDCANCEL, OnCancel)
    END_MSG_MAP()

    BOOL OnEraseBkgnd(CDCHandle dc) { navidrome::win::eraseLikeDialog(*this, dc); return TRUE; }

private:
    enum {
        IDC_NAME_LABEL = 4011, IDC_NAME_EDIT = 4012,
        IDC_URL_LABEL  = 4013, IDC_URL_EDIT  = 4014,
        IDC_HOME_LABEL = 4015, IDC_HOME_EDIT = 4016,
    };

    LRESULT OnCreate(LPCREATESTRUCT) {
        using namespace navidrome::win;
        const UiScale s(*this);
        const HFONT f = uiFont(*this);
        const int pad = s(12), fieldW = s(330), lineH = lineHeight(*this, f);
        const int rowH = (std::max)(s(22), lineH + s(8));

        int y = pad;
        auto field = [&](int labelId, const wchar_t* text, CEdit& edit, int editId,
                         const std::wstring& value) {
            setFont(CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE,
                pad, y, fieldW, lineH, *this,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(labelId)), nullptr, nullptr), f);
            y += lineH + s(4);
            edit.Create(*this, CWindow::rcDefault, nullptr,
                WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 0, editId);
            edit.SetWindowPos(nullptr, pad, y, fieldW, rowH, SWP_NOZORDER);
            edit.SetFont(f);
            edit.SetWindowText(value.c_str());
            y += rowH + s(10);
        };
        field(IDC_NAME_LABEL, L"Name:", m_nameEdit, IDC_NAME_EDIT, m_name);
        m_nameEdit.SetSel(0, -1);
        field(IDC_URL_LABEL, L"Stream URL:", m_urlEdit, IDC_URL_EDIT, m_streamURL);
        field(IDC_HOME_LABEL, L"Home page URL (optional):", m_homeEdit, IDC_HOME_EDIT, m_homePageURL);

        m_clientSize = layoutOkCancel(*this, f, s, pad + fieldW, y + s(2), rowH + s(4));
        m_darkMode.AddDialogWithControls(*this);
        return 0;
    }

    static std::wstring textOf(CEdit& edit) {
        int len = edit.GetWindowTextLength();
        std::wstring buf(static_cast<std::size_t>(len) + 1, L'\0');
        edit.GetWindowText(&buf[0], len + 1);
        buf.resize(static_cast<std::size_t>(len));
        return buf;
    }

    void OnOk(UINT, int, HWND) {
        m_name        = textOf(m_nameEdit);
        m_streamURL   = textOf(m_urlEdit);
        m_homePageURL = textOf(m_homeEdit);
        m_accepted    = true;
        m_done        = true;
    }

    void OnCancel(UINT, int, HWND) { m_done = true; }
    void OnClose()                 { m_done = true; }

    CEdit        m_nameEdit, m_urlEdit, m_homeEdit;
    std::wstring m_name, m_streamURL, m_homePageURL;
    SIZE         m_clientSize{};
    bool         m_accepted = false;
    bool         m_done     = false;
    fb2k::CCoreDarkModeHooks m_darkMode;
};
}

bool navidrome::win::promptText(HWND owner, const wchar_t* title, const wchar_t* label,
                                const std::wstring& initial, std::wstring& out) {
    return TextPromptWindow::run(owner, title, label, initial, out);
}

bool navidrome::win::promptRadioStation(HWND owner, const wchar_t* title,
                                        const std::wstring& initialName,
                                        const std::wstring& initialStreamURL,
                                        const std::wstring& initialHomePageURL,
                                        std::wstring& outName, std::wstring& outStreamURL,
                                        std::wstring& outHomePageURL) {
    return RadioStationPromptWindow::run(owner, title, initialName, initialStreamURL,
                                         initialHomePageURL, outName, outStreamURL,
                                         outHomePageURL);
}

bool navidrome::win::pickFolder(HWND owner, std::wstring& outPath) {
    wchar_t display[MAX_PATH] = {};
    BROWSEINFOW bi = {};
    bi.hwndOwner      = owner;
    bi.pszDisplayName = display;
    bi.lpszTitle      = L"Choose a folder for the downloaded tracks";
    bi.ulFlags        = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    HRESULT co = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    LPITEMIDLIST pidl = ::SHBrowseForFolderW(&bi);
    bool ok = false;
    if (pidl) {
        wchar_t path[MAX_PATH] = {};
        if (::SHGetPathFromIDListW(pidl, path)) { outPath = path; ok = true; }
        ::CoTaskMemFree(pidl);
    }
    if (SUCCEEDED(co)) ::CoUninitialize();
    return ok;
}

void navidrome::win::addDarkModeHooksKeepingLists(fb2k::CCoreDarkModeHooks& dark, HWND page) {
    dark.AddDialog(page);
    for (HWND c = ::GetWindow(page, GW_CHILD); c; c = ::GetWindow(c, GW_HWNDNEXT)) {
        wchar_t cls[64] = {};
        ::GetClassNameW(c, cls, 64);
        if (::lstrcmpiW(cls, WC_LISTVIEWW) != 0) { dark.AddCtrlAuto(c); continue; }
        if (!dark) continue;
        const COLORREF bg = RGB(0x20, 0x20, 0x20), text = RGB(0xDE, 0xDE, 0xDE);
        ListView_SetBkColor(c, bg);
        ListView_SetTextBkColor(c, bg);
        ListView_SetTextColor(c, text);
        ::SetWindowTheme(c, L"DarkMode_Explorer", nullptr);
        if (HWND header = ListView_GetHeader(c))
            ::SetWindowTheme(header, L"DarkMode_ItemsView", nullptr);
    }
}
