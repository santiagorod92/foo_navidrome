#pragma once

#include "../../core/SubsonicTypes.h"
#include <windows.h>
#include <winhttp.h>
#include <functional>
#include <string>

namespace navidrome::win {

struct WinHttpHandle {
    HINTERNET h = nullptr;
    WinHttpHandle() = default;
    explicit WinHttpHandle(HINTERNET handle) : h(handle) {}
    ~WinHttpHandle() { if (h) WinHttpCloseHandle(h); }
    WinHttpHandle(const WinHttpHandle&) = delete;
    WinHttpHandle& operator=(const WinHttpHandle&) = delete;
    WinHttpHandle(WinHttpHandle&& o) noexcept : h(o.h) { o.h = nullptr; }
    WinHttpHandle& operator=(WinHttpHandle&& o) noexcept {
        if (this != &o) { if (h) WinHttpCloseHandle(h); h = o.h; o.h = nullptr; }
        return *this;
    }
    operator HINTERNET() const { return h; }
    explicit operator bool() const { return h != nullptr; }
};

struct WinHttpTimeouts {
    int connectMs = 15000;
    int sendMs    = 15000;
    int receiveMs = 30000;
};

struct WinHttpRequest {
    WinHttpHandle session;
    WinHttpHandle connection;
    WinHttpHandle request;
    ErrorKind     errorKind = ErrorKind::None;
    std::string   error;
    bool ok() const { return static_cast<bool>(request); }
};

ErrorKind classifyWinHttpError(DWORD err);

WinHttpRequest openWinHttpRequest(const std::string& url, const wchar_t* verb,
                                  const WinHttpTimeouts& timeouts, const std::string& headerLines);

bool sendWinHttpRequest(HINTERNET request, const std::string* body, DWORD& lastError);

DWORD winHttpStatus(HINTERNET request);

std::string winHttpContentType(HINTERNET request);

bool readWinHttpBody(HINTERNET request, const std::function<bool(const char*, DWORD)>& sink);

std::string readWinHttpBody(HINTERNET request);

}
