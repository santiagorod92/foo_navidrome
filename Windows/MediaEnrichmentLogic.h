#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace navidrome {

std::string uriEncode(const std::string& value);
std::string uriDecode(const std::string& value);
std::string normalizeMediaServerUrl(const std::string& value);
// resolveArtId now lives in ../SubsonicTypes.h (shared with macOS).

std::string buildCoverArtUrl(const std::string& serverUrl,
                             const std::string& username,
                             const std::string& password,
                             const std::string& salt,
                             const std::string& coverId,
                             int size = 0);

enum class FetchClass {
    Ok,
    NotFound,
    Auth,
    ServerError,
    Transport,
    InvalidContent,
    Aborted,
};

FetchClass classifyHttpStatus(std::uint32_t status);
FetchClass classifyBody(const std::string& contentType,
                        const std::vector<std::uint8_t>& bytes,
                        std::size_t maxBytes = static_cast<std::size_t>(-1));

class CoverCache {
public:
    static CoverCache& instance();

    std::vector<std::uint8_t> get(const std::string& serverUrl,
                                  const std::string& username,
                                  const std::string& coverId);
    void put(const std::string& serverUrl,
             const std::string& username,
             const std::string& coverId,
             const std::vector<std::uint8_t>& bytes);
    void clear();

private:
    struct Entry {
        std::vector<std::uint8_t> bytes;
        std::uint64_t accessSequence = 0;
    };

    CoverCache() = default;
    std::string makeKey(const std::string& serverUrl,
                        const std::string& username,
                        const std::string& coverId) const;
    void evictLeastRecentlyUsed();

    std::mutex m_mutex;
    std::map<std::string, Entry> m_cache;
    std::size_t m_totalBytes = 0;
    std::uint64_t m_accessSequence = 0;

    static constexpr std::size_t kMaxEntries = 32;
    static constexpr std::size_t kMaxBytes = 48 * 1024 * 1024;
};

std::string jsEscape(const std::string& value);
std::string buildEsLyricConfigJs(
    const std::string& serverUrl,
    const std::string& username,
    const std::string& password,
    const std::string& salt,
    const std::vector<std::pair<std::string, std::string>>& headers,
    const std::string& componentVersion,
    bool debug = false);

} // namespace navidrome
