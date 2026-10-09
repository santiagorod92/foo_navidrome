#pragma once
#include <string>

namespace navidrome::win {

bool promptText(HWND owner, const wchar_t* title, const wchar_t* label,
                const std::wstring& initial, std::wstring& out);

bool promptRadioStation(HWND owner, const wchar_t* title,
                        const std::wstring& initialName,
                        const std::wstring& initialStreamURL,
                        const std::wstring& initialHomePageURL,
                        std::wstring& outName, std::wstring& outStreamURL,
                        std::wstring& outHomePageURL);

bool pickFolder(HWND owner, std::wstring& outPath);

void addDarkModeHooksKeepingLists(fb2k::CCoreDarkModeHooks& dark, HWND page);
}
