#include "TestHarness.h"
#include "../src/core/OfflineCache.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <set>

namespace fs = std::filesystem;
using namespace navidrome::offline;

namespace {

struct TempRoot {
    fs::path path;
    TempRoot() {
        std::random_device rd;
        path = fs::temp_directory_path() / ("foo_navidrome_cache_test_" + std::to_string(rd()));
        fs::create_directories(path);
    }
    ~TempRoot() { std::error_code ec; fs::remove_all(path, ec); }
    std::string str() const { return path.string(); }
};

void writeFile(const std::string& p, std::size_t bytes) {
    std::ofstream out(p, std::ios::binary);
    out << std::string(bytes, 'x');
}

void download(const Store& store, Slot slot, const std::string& id, const std::string& suffix, std::size_t bytes) {
    writeFile(store.partPath(slot, id, suffix), bytes);
    std::string err;
    check(store.commit(slot, id, suffix, err), "commit succeeds");
}

}

TEST_CASE(testOfflineCacheFileName) {
    check(cacheFileName("abc123", "flac") == "abc123.flac", "id + suffix");
    check(cacheFileName("a/b:c", "mp3") == "a_b_c.mp3", "id sanitized");
    check(cacheFileName("", "mp3").empty(), "no id, no name");
    check(cacheFileName("abc", "").empty(), "no suffix, no name");
}

TEST_CASE(testOfflineCacheEvictionPlan) {
    std::vector<CacheFile> files = {
        { "old.mp3", 400, 10 }, { "mid.mp3", 400, 20 }, { "new.mp3", 400, 30 }, { "newest.mp3", 400, 40 },
    };
    check(evictionPlan(files, 2000).empty(), "under the limit evicts nothing");

    auto plan = evictionPlan(files, 900);
    check(plan.size() == 2 && plan[0] == "old.mp3" && plan[1] == "mid.mp3", "oldest first until under the limit");

    plan = evictionPlan(files, 0);
    check(plan.size() == 2, "the two newest files are always kept");
    check(std::set<std::string>(plan.begin(), plan.end()) == std::set<std::string>{ "old.mp3", "mid.mp3" },
          "kept files are the newest ones");

    plan = evictionPlan(files, 0, 0);
    check(plan.size() == 4, "keepNewest 0 can empty the cache");
}

TEST_CASE(testOfflineCacheServerErrorBody) {
    const std::string err = R"({"subsonic-response":{"status":"failed","version":"1.16.1","error":{"code":70,"message":"data not found"}}})";
    check(serverErrorInBody(err) == "data not found", "JSON Subsonic error is detected with its message");
    check(!serverErrorInBody("<?xml version=\"1.0\"?><subsonic-response status=\"failed\"/>").empty(), "XML error is detected");
    check(serverErrorInBody(std::string("ID3\x04\x00", 5)).empty(), "an MP3 is audio");
    check(serverErrorInBody("fLaC\x00\x00").empty(), "a FLAC is audio");
    check(serverErrorInBody("").empty(), "an empty head isn't an error body");

    TempRoot root;
    Store store(root.str());
    writeFile(store.partPath(Slot::Prefetch, "gone", "mp3"), 0);
    { std::ofstream(store.partPath(Slot::Prefetch, "gone", "mp3"), std::ios::binary) << err; }
    std::string why;
    check(!store.commit(Slot::Prefetch, "gone", "mp3", why) && why == "data not found", "an error body is never committed");
    check(!store.has(Slot::Prefetch, "gone", "mp3") && store.usage().prefetchFiles == 0, "and nothing is left behind");
}

TEST_CASE(testOfflineCacheFormatBytes) {
    check(formatBytes(512) == "512 B", "bytes");
    check(formatBytes(2048) == "2 KB", "kilobytes");
    check(formatBytes(340ull << 20) == "340 MB", "megabytes");
    check(formatBytes(3ull << 29) == "1.5 GB", "gigabytes");
}

TEST_CASE(testOfflineCacheStoreFindAndCommit) {
    TempRoot root;
    Store store(root.str());

    check(store.find("s1", { "flac" }).empty(), "empty cache finds nothing");

    writeFile(store.partPath(Slot::Prefetch, "s1", "opus"), 10);
    check(store.find("s1", { "opus" }).empty(), "a partial download is never played");

    std::string err;
    check(store.commit(Slot::Prefetch, "s1", "opus", err), "commit renames the part file");
    check(store.has(Slot::Prefetch, "s1", "opus"), "committed file is present");
    check(!store.find("s1", { "flac", "opus" }).empty(), "found by any candidate suffix");

    download(store, Slot::Pinned, "s1", "flac", 20);
    const std::string hit = store.find("s1", { "flac", "opus" });
    check(hit.find("pinned") != std::string::npos && hit.size() > 5 && hit.substr(hit.size() - 5) == ".flac",
          "pinned copy wins over the prefetched one");

    check(!store.commit(Slot::Pinned, "missing", "mp3", err) && !err.empty(), "commit without a part file fails");
}

TEST_CASE(testOfflineCacheStoreUsageEvictRemove) {
    TempRoot root;
    Store store(root.str());
    download(store, Slot::Pinned, "p1", "flac", 100);
    download(store, Slot::Pinned, "p10", "flac", 100);
    for (int i = 0; i < 4; ++i) {
        download(store, Slot::Prefetch, "f" + std::to_string(i), "mp3", 100);
        const fs::path p = root.path / "prefetch" / ("f" + std::to_string(i) + ".mp3");
        fs::last_write_time(p, fs::file_time_type::clock::now() - std::chrono::hours(10 - i));
    }
    writeFile(store.partPath(Slot::Prefetch, "busy", "mp3"), 999);

    Usage u = store.usage();
    check(u.pinnedFiles == 2 && u.pinnedBytes == 200, "pinned usage");
    check(u.prefetchFiles == 4 && u.prefetchBytes == 400, "prefetch usage ignores part files");

    check(store.evictPrefetch(250) == 2, "evicts down to the limit");
    check(!store.has(Slot::Prefetch, "f0", "mp3") && !store.has(Slot::Prefetch, "f1", "mp3"), "oldest evicted");
    check(store.has(Slot::Prefetch, "f3", "mp3"), "newest kept");
    check(store.usage().pinnedFiles == 2, "eviction never touches pinned copies");

    check(store.removePinned("p1") == 1, "remove matches the exact id, not a prefix");
    check(store.has(Slot::Pinned, "p10", "flac"), "p10 survives removing p1");

    check(store.clearPrefetch() == 3, "clear removes finished and partial prefetch files");
    check(store.usage().prefetchFiles == 0, "prefetch empty after clear");
}

TEST_CASE(testOfflineCacheDeferredRemoval) {
    TempRoot root;
    Store store(root.str());
    download(store, Slot::Pinned, "busy", "flac", 100);
    download(store, Slot::Pinned, "keep", "flac", 100);
    const std::string busy = (root.path / "pinned" / "busy.flac").string();

    writeFile(busy + ".removed", 0);
    check(!store.has(Slot::Pinned, "busy", "flac"), "a file marked removed isn't there any more");
    check(store.find("busy", { "flac" }).empty(), "and is never played");
    check(store.usage().pinnedFiles == 1 && store.usage().pinnedBytes == 100, "nor counted");
    check(store.removePinned("busy") == 0, "nor removed twice");

    std::size_t pending = 0;
    check(store.purgeRemoved(&pending) == 1 && pending == 0, "purge deletes the file and its marker");
    check(!fs::exists(busy) && !fs::exists(busy + ".removed"), "both are gone");
    check(store.has(Slot::Pinned, "keep", "flac"), "other pinned copies stay");

    writeFile(busy + ".removed", 0);
    download(store, Slot::Pinned, "busy", "flac", 100);
    check(store.has(Slot::Pinned, "busy", "flac") && !fs::exists(busy + ".removed"),
          "pinning again clears a stale marker");

#ifdef _WIN32
    {
        std::ifstream open(busy, std::ios::binary);
        std::size_t deferred = 0;
        check(store.removePinned("busy", &deferred) == 1 && deferred == 1,
              "a copy in use is marked instead of deleted");
        check(!store.has(Slot::Pinned, "busy", "flac"), "and looks removed right away");
        pending = 0;
        check(store.purgeRemoved(&pending) == 0 && pending == 1, "purge waits while it's open");
    }
    pending = 0;
    check(store.purgeRemoved(&pending) == 1 && pending == 0 && !fs::exists(busy), "and deletes it once closed");
#endif
}
