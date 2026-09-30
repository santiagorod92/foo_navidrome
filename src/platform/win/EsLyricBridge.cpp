#include "stdafx.h"
#include "EsLyricBridge.h"

#include "EsLyricScript.h"
#include "../../core/MediaEnrichmentLogic.h"
#include "SubsonicClientWin.h"

#if __has_include("version_generated.h")
#include "version_generated.h"
#endif
#ifndef COMPONENT_VERSION
#define COMPONENT_VERSION "1.0.0"
#endif

#include <SDK/filesystem.h>
#include <shlobj.h>

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

namespace navidrome {
namespace {

std::wstring utf8ToWide(const std::string& value) {
    if (value.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) return {};
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
            static_cast<int>(value.size()), result.data(), count) != count) {
        return {};
    }
    return result;
}

std::wstring profileNativePath(const char* relativePath) {
    const auto profileUrl = core_api::pathInProfile(relativePath);
    const auto nativePath = filesystem::g_get_native_path(profileUrl.c_str());
    return utf8ToWide(nativePath.c_str());
}

std::wstring joinPath(std::wstring base, const wchar_t* suffix) {
    if (!base.empty() && base.back() != L'\\' && base.back() != L'/')
        base.push_back(L'\\');
    base += suffix;
    return base;
}

bool isDirectory(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool ensureDirectory(const std::wstring& path) {
    if (path.empty()) return false;
    const int result = SHCreateDirectoryExW(nullptr, path.c_str(), nullptr);
    return result == ERROR_SUCCESS || result == ERROR_ALREADY_EXISTS ||
        isDirectory(path);
}

bool readFile(const std::wstring& path, std::string& content) {
    content.clear();
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER size = {};
    const bool sizeOk = GetFileSizeEx(file, &size) != FALSE &&
        size.QuadPart >= 0 && size.QuadPart <= 4 * 1024 * 1024;
    if (!sizeOk) {
        CloseHandle(file);
        return false;
    }

    content.resize(static_cast<std::size_t>(size.QuadPart));
    DWORD totalRead = 0;
    while (totalRead < content.size()) {
        DWORD read = 0;
        const DWORD wanted = static_cast<DWORD>(std::min<std::size_t>(
            content.size() - totalRead, 64 * 1024));
        if (!ReadFile(file, content.data() + totalRead, wanted, &read, nullptr) ||
            read == 0) {
            CloseHandle(file);
            content.clear();
            return false;
        }
        totalRead += read;
    }
    CloseHandle(file);
    return true;
}

bool writeFileAtomically(const std::wstring& path, const std::string& content) {
    std::string existing;
    if (readFile(path, existing) && existing == content) return true;

    const auto separator = path.find_last_of(L"\\/");
    if (separator == std::wstring::npos ||
        !ensureDirectory(path.substr(0, separator))) {
        return false;
    }

    const std::wstring tempPath = path + L"." +
        std::to_wstring(GetCurrentProcessId()) + L"." +
        std::to_wstring(GetCurrentThreadId()) + L".tmp";
    HANDLE file = CreateFileW(tempPath.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    std::size_t offset = 0;
    bool ok = true;
    while (offset < content.size()) {
        DWORD written = 0;
        const DWORD wanted = static_cast<DWORD>(std::min<std::size_t>(
            content.size() - offset, 64 * 1024));
        if (!WriteFile(file, content.data() + offset, wanted, &written, nullptr) ||
            written == 0) {
            ok = false;
            break;
        }
        offset += written;
    }
    if (ok) ok = FlushFileBuffers(file) != FALSE;
    CloseHandle(file);

    if (ok) {
        ok = MoveFileExW(tempPath.c_str(), path.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    }
    if (!ok) DeleteFileW(tempPath.c_str());
    return ok;
}

std::vector<std::pair<std::string, std::string>> parseHeaders(
        const std::string& blob) {
    std::vector<std::pair<std::string, std::string>> result;
    for (const auto& line : parseHeaderLines(blob)) {
        const auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        auto name = line.substr(0, colon);
        auto value = line.substr(colon + 1);
        const auto trim = [](std::string& text) {
            const auto begin = text.find_first_not_of(" \t");
            if (begin == std::string::npos) {
                text.clear();
                return;
            }
            const auto end = text.find_last_not_of(" \t");
            text = text.substr(begin, end - begin + 1);
        };
        trim(name);
        trim(value);
        if (!name.empty()) result.emplace_back(std::move(name), std::move(value));
    }
    return result;
}

} // namespace

bool EsLyricBridge::isEsLyricInstalled() {
    return isDirectory(profileNativePath("eslyric-data"));
}

std::string EsLyricBridge::installOrUpdate(
        const SubsonicRequestContext& context, bool debug) {
    const auto basePath = profileNativePath("eslyric-data");
    if (!isDirectory(basePath)) return {};

    const auto searcherPath = joinPath(basePath, L"scripts\\searcher\\navidrome.js");
    const auto configPath = joinPath(basePath,
        L"scripts\\lib\\foo_navidrome\\config.js");
    const auto config = buildEsLyricConfigJs(context.serverUrl, context.username,
        context.password, context.salt, parseHeaders(context.customHeaders),
        COMPONENT_VERSION, debug);

    if (!writeFileAtomically(configPath, config))
        return "Failed to write ESLyric config file";
    if (!writeFileAtomically(searcherPath, kEsLyricScriptSource))
        return "Failed to write ESLyric searcher script";
    return {};
}

} // namespace navidrome
