#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace navidrome::offline {

enum class Slot { Pinned, Prefetch };

constexpr std::uint64_t kDefaultPrefetchLimitMB = 1024;
constexpr std::size_t   kPrefetchKeepNewest     = 2;

struct CacheFile {
    std::string   name;
    std::uint64_t size  = 0;
    std::int64_t  mtime = 0;
};

struct Usage {
    std::size_t   pinnedFiles   = 0;
    std::uint64_t pinnedBytes   = 0;
    std::size_t   prefetchFiles = 0;
    std::uint64_t prefetchBytes = 0;
};

struct Item {
    std::string id;
    std::string suffix;
    std::string title;
};

std::filesystem::path fsPath(const std::string& utf8);

std::string cacheFileName(const std::string& id, const std::string& suffix);

std::vector<std::string> evictionPlan(std::vector<CacheFile> files, std::uint64_t limitBytes,
                                      std::size_t keepNewest = kPrefetchKeepNewest);

std::string formatBytes(std::uint64_t bytes);

std::string serverErrorInBody(const std::string& head);

class Store {
public:
    explicit Store(std::string rootUtf8);

    const std::string& root() const { return m_root; }

    std::string find(const std::string& id, const std::vector<std::string>& suffixes) const;
    bool        has(Slot slot, const std::string& id, const std::string& suffix) const;
    std::string partPath(Slot slot, const std::string& id, const std::string& suffix) const;
    bool        commit(Slot slot, const std::string& id, const std::string& suffix, std::string& error) const;
    void        discardPart(Slot slot, const std::string& id, const std::string& suffix) const;
    std::size_t removePinned(const std::string& id, std::size_t* deferred = nullptr) const;
    std::size_t purgeRemoved(std::size_t* pending = nullptr) const;
    std::size_t evictPrefetch(std::uint64_t limitBytes) const;
    std::size_t clearPrefetch() const;
    Usage       usage() const;

private:
    std::string dir(Slot slot) const;
    std::string m_root;
};

}
