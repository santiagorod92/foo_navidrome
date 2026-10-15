#include "stdafx.h"
#include "WinHttp.h"
#include "WinUi.h"
#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace navidrome::win {

namespace {

void applySecureProtocols(HINTERNET session) {
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
    protocols |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
    WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
}

}

ErrorKind classifyWinHttpError(DWORD err) {
    switch (err) {
        case 12002:
            return ErrorKind::Timeout;
        case 12157:
        case 12175:
            return ErrorKind::Tls;
        default:
            return ErrorKind::Network;
    }
}

WinHttpRequest openWinHttpRequest(const std::string& url, const wchar_t* verb,
                                  const WinHttpTimeouts& timeouts, const std::string& headerLines) {
    WinHttpRequest out;
    const std::wstring wurl = u8ToWide(url);
    URL_COMPONENTS uc = {};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256] = {}, path[4096] = {};
    uc.lpszHostName = host; uc.dwHostNameLength = 256;
    uc.lpszUrlPath  = path; uc.dwUrlPathLength  = 4096;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) {
        out.errorKind = ErrorKind::Parse;
        out.error = "Invalid URL";
        return out;
    }

    out.session = WinHttpHandle(WinHttpOpen(L"foo_navidrome/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!out.session) {
        out.errorKind = ErrorKind::Network;
        out.error = "WinHttpOpen failed";
        return out;
    }
    WinHttpSetTimeouts(out.session, 0, timeouts.connectMs, timeouts.sendMs, timeouts.receiveMs);
    applySecureProtocols(out.session);

    out.connection = WinHttpHandle(WinHttpConnect(out.session, host, uc.nPort, 0));
    if (!out.connection) {
        out.errorKind = classifyWinHttpError(GetLastError());
        out.error = "Connect failed";
        return out;
    }

    const DWORD flags = uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    out.request = WinHttpHandle(WinHttpOpenRequest(out.connection, verb, path, nullptr,
                                                   WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
    if (!out.request) {
        out.errorKind = ErrorKind::Network;
        out.error = "WinHttpOpenRequest failed (err=" + std::to_string(GetLastError()) + ")";
        return out;
    }

    const std::string joined = joinHeaderLines(headerLines);
    if (!joined.empty()) {
        const std::wstring wide = u8ToWide(joined);
        WinHttpAddRequestHeaders(out.request, wide.c_str(), (DWORD)-1,
                                 WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
    }
    return out;
}

bool sendWinHttpRequest(HINTERNET request, const std::string* body, DWORD& lastError) {
    const DWORD size = body ? static_cast<DWORD>(body->size()) : 0;
    LPVOID data = body ? const_cast<char*>(body->data()) : nullptr;
    if (WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, data, size, size, 0) &&
        WinHttpReceiveResponse(request, nullptr)) {
        lastError = 0;
        return true;
    }
    lastError = GetLastError();
    return false;
}

DWORD winHttpStatus(HINTERNET request) {
    DWORD status = 0, sz = sizeof(status);
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             nullptr, &status, &sz, nullptr))
        return 0;
    return status;
}

std::string winHttpContentType(HINTERNET request) {
    wchar_t buf[256] = {};
    DWORD len = sizeof(buf);
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_TYPE, nullptr, buf, &len, nullptr))
        return {};
    return wToU8(buf);
}

bool readWinHttpBody(HINTERNET request, const std::function<bool(const char*, DWORD)>& sink) {
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(request, &avail)) return false;
        if (avail == 0) return true;
        std::vector<char> chunk(avail);
        DWORD read = 0;
        if (!WinHttpReadData(request, chunk.data(), avail, &read)) return false;
        if (!sink(chunk.data(), read)) return false;
    }
}

std::string readWinHttpBody(HINTERNET request) {
    std::string body;
    readWinHttpBody(request, [&](const char* p, DWORD n) { body.append(p, n); return true; });
    return body;
}

}
