// This module is deliberately SDK-free (see CLAUDE.md) so the unit-test host in
// Windows/tests/ can build it standalone. The only platform-specific piece is
// the MD5 primitive: WinCrypt on Windows, CommonCrypto on macOS (the same one
// SubsonicClient.mm already uses). That keeps the whole test suite building and
// running natively on both platforms — `make test` (clang-cl + wine) and
// `make mac-test` (native clang++) — off the one source file.
#if defined(_WIN32)
#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wincrypt.h>
#else
#include <CommonCrypto/CommonDigest.h>
#endif

#include "MediaEnrichmentLogic.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <iomanip>
#include <sstream>

#if defined(_WIN32)
#pragma comment(lib, "advapi32.lib")
#endif

namespace navidrome {
namespace {

#if defined(_WIN32)
std::string md5Hex(const std::string& input) {
    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    if (!CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_FULL,
                              CRYPT_VERIFYCONTEXT)) return {};
    if (!CryptCreateHash(provider, CALG_MD5, 0, 0, &hash)) {
        CryptReleaseContext(provider, 0);
        return {};
    }
    const bool updated = CryptHashData(
        hash, reinterpret_cast<const BYTE*>(input.data()),
        static_cast<DWORD>(input.size()), 0) != FALSE;
    BYTE digest[16] = {};
    DWORD digestSize = sizeof(digest);
    const bool read = updated &&
        CryptGetHashParam(hash, HP_HASHVAL, digest, &digestSize, 0) != FALSE;
    CryptDestroyHash(hash);
    CryptReleaseContext(provider, 0);
    if (!read || digestSize != sizeof(digest)) return {};

    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (BYTE byte : digest) output << std::setw(2) << static_cast<unsigned>(byte);
    return output.str();
}
#else
std::string md5Hex(const std::string& input) {
    unsigned char digest[CC_MD5_DIGEST_LENGTH] = {};
    // CC_MD5 is deprecated on modern macOS but not removed; it stays the
    // lowest-dependency MD5 (no framework link) and matches SubsonicClient.mm.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    CC_MD5(input.data(), static_cast<CC_LONG>(input.size()), digest);
#pragma clang diagnostic pop

    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (unsigned char byte : digest)
        output << std::setw(2) << static_cast<unsigned>(byte);
    return output.str();
}
#endif

std::string queryParameter(const std::string& query, const std::string& key) {
    std::size_t offset = 0;
    while (offset <= query.size()) {
        const auto separator = query.find('&', offset);
        const auto pair = query.substr(offset, separator == std::string::npos
            ? std::string::npos : separator - offset);
        const auto equals = pair.find('=');
        if (equals != std::string::npos && pair.substr(0, equals) == key)
            return uriDecode(pair.substr(equals + 1));
        if (separator == std::string::npos) break;
        offset = separator + 1;
    }
    return {};
}

bool startsWithCaseInsensitive(const std::string& value, const char* prefix) {
    const std::size_t length = std::strlen(prefix);
    if (value.size() < length) return false;
    for (std::size_t index = 0; index < length; ++index) {
        if (std::tolower(static_cast<unsigned char>(value[index])) !=
            std::tolower(static_cast<unsigned char>(prefix[index]))) return false;
    }
    return true;
}

bool startsWithBytes(const std::vector<std::uint8_t>& bytes,
                     const std::uint8_t* expected, std::size_t count) {
    return bytes.size() >= count && std::memcmp(bytes.data(), expected, count) == 0;
}

int subsonicErrorCode(const std::vector<std::uint8_t>& bytes) {
    const std::string text(bytes.begin(), bytes.end());
    if (text.find("subsonic-response") == std::string::npos &&
        text.find("subsonic_response") == std::string::npos) return -1;
    auto position = text.find("\"code\"");
    if (position == std::string::npos) position = text.find("'code'");
    if (position == std::string::npos) position = text.find("code=");
    if (position == std::string::npos) return -1;
    position = text.find_first_of("0123456789", position + 5);
    if (position == std::string::npos) return -1;
    int value = 0;
    while (position < text.size() && std::isdigit(static_cast<unsigned char>(text[position]))) {
        value = value * 10 + (text[position] - '0');
        ++position;
    }
    return value;
}

} // namespace

std::string uriEncode(const std::string& value) {
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(value.size());
    for (unsigned char byte : value) {
        if (std::isalnum(byte) || byte == '-' || byte == '_' || byte == '.' || byte == '~') {
            result.push_back(static_cast<char>(byte));
        } else {
            result.push_back('%');
            result.push_back(hex[byte >> 4]);
            result.push_back(hex[byte & 0x0f]);
        }
    }
    return result;
}

std::string uriDecode(const std::string& value) {
    auto hexValue = [](char value) -> int {
        if (value >= '0' && value <= '9') return value - '0';
        if (value >= 'a' && value <= 'f') return value - 'a' + 10;
        if (value >= 'A' && value <= 'F') return value - 'A' + 10;
        return -1;
    };
    std::string result;
    result.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] == '%' && index + 2 < value.size()) {
            const int high = hexValue(value[index + 1]);
            const int low = hexValue(value[index + 2]);
            if (high >= 0 && low >= 0) {
                result.push_back(static_cast<char>((high << 4) | low));
                index += 2;
                continue;
            }
        }
        result.push_back(value[index]);
    }
    return result;
}

std::string normalizeMediaServerUrl(const std::string& value) {
    std::string result = value;
    while (!result.empty() && (result.front() == ' ' || result.front() == '\t'))
        result.erase(result.begin());
    while (!result.empty() && (result.back() == '/' || result.back() == ' ' ||
                               result.back() == '\t')) result.pop_back();
    const auto schemeEnd = result.find("://");
    if (schemeEnd == std::string::npos) return result;
    const auto authorityEnd = result.find('/', schemeEnd + 3);
    const auto lowerEnd = authorityEnd == std::string::npos ? result.size() : authorityEnd;
    std::transform(result.begin(), result.begin() + lowerEnd, result.begin(),
        [](unsigned char byte) { return static_cast<char>(std::tolower(byte)); });
    return result;
}

std::string resolveArtId(const std::string& path) {
    const auto queryStart = path.find('?');
    if (queryStart != std::string::npos) {
        const auto query = path.substr(queryStart + 1);
        auto value = queryParameter(query, "coverArt");
        if (!value.empty()) return value;
        value = queryParameter(query, "id");
        if (!value.empty()) return value;
    }
    static constexpr char prefix[] = "navidrome://track/";
    if (path.compare(0, sizeof(prefix) - 1, prefix) == 0) {
        const auto begin = sizeof(prefix) - 1;
        const auto end = path.find('?', begin);
        return uriDecode(path.substr(begin, end == std::string::npos
            ? std::string::npos : end - begin));
    }
    return {};
}

std::string buildCoverArtUrl(const std::string& serverUrl,
                             const std::string& username,
                             const std::string& password,
                             const std::string& salt,
                             const std::string& coverId,
                             int size) {
    auto result = normalizeMediaServerUrl(serverUrl) + "/rest/getCoverArt.view?u=" +
        uriEncode(username) + "&t=" + md5Hex(password + salt) + "&s=" + uriEncode(salt) +
        "&v=1.16.1&c=foo_navidrome&f=json&id=" + uriEncode(coverId);
    if (size > 0) result += "&size=" + std::to_string(size);
    return result;
}

FetchClass classifyHttpStatus(std::uint32_t status) {
    if (status == 200) return FetchClass::Ok;
    if (status == 401 || status == 403) return FetchClass::Auth;
    if (status == 404 || status == 410) return FetchClass::NotFound;
    if (status >= 500 && status < 600) return FetchClass::ServerError;
    return FetchClass::Transport;
}

FetchClass classifyBody(const std::string& contentType,
                        const std::vector<std::uint8_t>& bytes,
                        std::size_t maxBytes) {
    if (bytes.empty() || bytes.size() > maxBytes) return FetchClass::InvalidContent;
    const int errorCode = subsonicErrorCode(bytes);
    if (errorCode == 70) return FetchClass::NotFound;
    if (errorCode == 40 || errorCode == 41 || errorCode == 44) return FetchClass::Auth;
    if (errorCode > 0) return FetchClass::ServerError;

    static constexpr std::uint8_t jpeg[] = {0xff, 0xd8, 0xff};
    static constexpr std::uint8_t png[] = {0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};
    static constexpr std::uint8_t gif[] = {'G', 'I', 'F', '8'};
    static constexpr std::uint8_t bmp[] = {'B', 'M'};
    const bool knownMagic = startsWithBytes(bytes, jpeg, sizeof(jpeg)) ||
        startsWithBytes(bytes, png, sizeof(png)) ||
        startsWithBytes(bytes, gif, sizeof(gif)) ||
        startsWithBytes(bytes, bmp, sizeof(bmp)) ||
        (bytes.size() >= 12 && std::memcmp(bytes.data(), "RIFF", 4) == 0 &&
         std::memcmp(bytes.data() + 8, "WEBP", 4) == 0);
    if (knownMagic || startsWithCaseInsensitive(contentType, "image/")) return FetchClass::Ok;
    return FetchClass::InvalidContent;
}

CoverCache& CoverCache::instance() {
    static CoverCache cache;
    return cache;
}

std::string CoverCache::makeKey(const std::string& serverUrl,
                                const std::string& username,
                                const std::string& coverId) const {
    const auto server = normalizeMediaServerUrl(serverUrl);
    return std::to_string(server.size()) + ":" + server +
        std::to_string(username.size()) + ":" + username +
        std::to_string(coverId.size()) + ":" + coverId;
}

std::vector<std::uint8_t> CoverCache::get(const std::string& serverUrl,
                                          const std::string& username,
                                          const std::string& coverId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto found = m_cache.find(makeKey(serverUrl, username, coverId));
    if (found == m_cache.end()) return {};
    found->second.accessSequence = ++m_accessSequence;
    return found->second.bytes;
}

void CoverCache::put(const std::string& serverUrl,
                     const std::string& username,
                     const std::string& coverId,
                     const std::vector<std::uint8_t>& bytes) {
    if (bytes.empty() || bytes.size() > kMaxBytes) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto key = makeKey(serverUrl, username, coverId);
    auto& entry = m_cache[key];
    m_totalBytes -= entry.bytes.size();
    entry.bytes = bytes;
    entry.accessSequence = ++m_accessSequence;
    m_totalBytes += entry.bytes.size();
    while (m_cache.size() > kMaxEntries || m_totalBytes > kMaxBytes)
        evictLeastRecentlyUsed();
}

void CoverCache::evictLeastRecentlyUsed() {
    if (m_cache.empty()) return;
    const auto oldest = std::min_element(m_cache.begin(), m_cache.end(),
        [](const auto& left, const auto& right) {
            return left.second.accessSequence < right.second.accessSequence;
        });
    m_totalBytes -= oldest->second.bytes.size();
    m_cache.erase(oldest);
}

void CoverCache::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cache.clear();
    m_totalBytes = 0;
    m_accessSequence = 0;
}

std::string jsEscape(const std::string& value) {
    std::ostringstream result;
    for (unsigned char byte : value) {
        switch (byte) {
        case '\\': result << "\\\\"; break;
        case '"': result << "\\\""; break;
        case '\n': result << "\\n"; break;
        case '\r': result << "\\r"; break;
        case '\t': result << "\\t"; break;
        default:
            if (byte < 0x20) {
                result << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                       << static_cast<unsigned>(byte) << std::dec;
            } else {
                result << static_cast<char>(byte);
            }
        }
    }
    return result.str();
}

std::string buildEsLyricConfigJs(
    const std::string& serverUrl,
    const std::string& username,
    const std::string& password,
    const std::string& salt,
    const std::vector<std::pair<std::string, std::string>>& headers,
    const std::string& componentVersion,
    bool debug) {
    std::ostringstream result;
    result << "// generated by foo_navidrome " << jsEscape(componentVersion)
           << " - do not edit; restart foobar2000 after settings changes\n"
           << "export const config = {\n"
           << "  serverUrl: \"" << jsEscape(normalizeMediaServerUrl(serverUrl)) << "\",\n"
           << "  username: \"" << jsEscape(username) << "\",\n"
           << "  token: \"" << md5Hex(password + salt) << "\",\n"
           << "  salt: \"" << jsEscape(salt) << "\",\n"
           << "  componentVersion: \"" << jsEscape(componentVersion) << "\",\n"
           << "  headers: {";
    for (std::size_t index = 0; index < headers.size(); ++index) {
        if (index != 0) result << ", ";
        result << "\"" << jsEscape(headers[index].first) << "\": \""
               << jsEscape(headers[index].second) << "\"";
    }
    result << "},\n  debug: " << (debug ? "true" : "false") << ",\n};\n";
    return result.str();
}

} // namespace navidrome
