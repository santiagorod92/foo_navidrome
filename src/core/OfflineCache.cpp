#include "OfflineCache.h"
#include "TrackUri.h"
#include "Json.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace fs = std::filesystem;

namespace navidrome::offline {

fs::path fsPath(const std::string& utf8) {
#if defined(__cpp_char8_t)
    return fs::path(std::u8string(utf8.begin(), utf8.end()));
#else
    return fs::u8path(utf8);
#endif
}

namespace {

constexpr const char* kPartSuffix    = ".part";
constexpr const char* kRemovedSuffix = ".removed";

fs::path toPath(const std::string& utf8) { return fsPath(utf8); }

std::string toUtf8(const fs::path& p) {
    const auto s = p.u8string();
    return std::string(s.begin(), s.end());
}

bool endsWith(const std::string& s, const char* suffix) {
    const std::size_t n = std::char_traits<char>::length(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

bool isPart(const fs::path& p) { return endsWith(toUtf8(p.filename()), kPartSuffix); }

bool isRemoved(const fs::path& p) {
    std::error_code ec;
    return endsWith(toUtf8(p.filename()), kRemovedSuffix)
        || fs::exists(fs::path(p).concat(kRemovedSuffix), ec);
}

std::int64_t mtimeOf(const fs::path& p) {
    std::error_code ec;
    const auto t = fs::last_write_time(p, ec);
    if (ec) return 0;
    return std::chrono::duration_cast<std::chrono::seconds>(t.time_since_epoch()).count();
}

std::vector<CacheFile> listFiles(const fs::path& dir) {
    std::vector<CacheFile> out;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code fec;
        if (!it->is_regular_file(fec) || isPart(it->path()) || isRemoved(it->path())) continue;
        CacheFile f;
        f.name  = toUtf8(it->path().filename());
        f.size  = static_cast<std::uint64_t>(it->file_size(fec));
        f.mtime = mtimeOf(it->path());
        out.push_back(std::move(f));
    }
    return out;
}

}

std::string cacheFileName(const std::string& id, const std::string& suffix) {
    if (id.empty() || suffix.empty()) return {};
    return sanitizeFileName(id) + "." + sanitizeFileName(suffix);
}

std::vector<std::string> evictionPlan(std::vector<CacheFile> files, std::uint64_t limitBytes,
                                      std::size_t keepNewest) {
    std::sort(files.begin(), files.end(), [](const CacheFile& a, const CacheFile& b) {
        if (a.mtime != b.mtime) return a.mtime > b.mtime;
        return a.name < b.name;
    });
    std::uint64_t total = 0;
    for (const auto& f : files) total += f.size;

    std::vector<std::string> evict;
    for (std::size_t i = files.size(); i > keepNewest && total > limitBytes; --i) {
        const CacheFile& f = files[i - 1];
        evict.push_back(f.name);
        total -= f.size;
    }
    return evict;
}

std::string serverErrorInBody(const std::string& head) {
    std::size_t i = 0;
    while (i < head.size() && (head[i] == ' ' || head[i] == '\n' || head[i] == '\r' || head[i] == '\t')) ++i;
    if (i >= head.size() || (head[i] != '{' && head[i] != '<')) return {};
    if (head.find("subsonic-response") == std::string::npos) return {};
    std::string parseErr;
    const json::Value root = json::parse(head.substr(i), parseErr);
    const std::string message = root["subsonic-response"]["error"]["message"].asString();
    return message.empty() ? std::string("the server returned an error instead of audio") : message;
}

std::string formatBytes(std::uint64_t bytes) {
    char buf[32];
    const double v = static_cast<double>(bytes);
    if (bytes >= (1ull << 30))      std::snprintf(buf, sizeof buf, "%.1f GB", v / (1ull << 30));
    else if (bytes >= (1ull << 20)) std::snprintf(buf, sizeof buf, "%.0f MB", v / (1ull << 20));
    else if (bytes >= (1ull << 10)) std::snprintf(buf, sizeof buf, "%.0f KB", v / (1ull << 10));
    else                            std::snprintf(buf, sizeof buf, "%llu B", static_cast<unsigned long long>(bytes));
    return buf;
}

Store::Store(std::string rootUtf8) : m_root(std::move(rootUtf8)) {}

std::string Store::dir(Slot slot) const {
    return toUtf8(toPath(m_root) / (slot == Slot::Pinned ? "pinned" : "prefetch"));
}

std::string Store::find(const std::string& id, const std::vector<std::string>& suffixes) const {
    for (Slot slot : { Slot::Pinned, Slot::Prefetch }) {
        for (const auto& suffix : suffixes) {
            const std::string name = cacheFileName(id, suffix);
            if (name.empty()) continue;
            const fs::path p = toPath(dir(slot)) / toPath(name);
            std::error_code ec;
            if (!fs::is_regular_file(p, ec) || isRemoved(p)) continue;
            fs::last_write_time(p, fs::file_time_type::clock::now(), ec);
            return toUtf8(p);
        }
    }
    return {};
}

bool Store::has(Slot slot, const std::string& id, const std::string& suffix) const {
    const std::string name = cacheFileName(id, suffix);
    if (name.empty()) return false;
    const fs::path p = toPath(dir(slot)) / toPath(name);
    std::error_code ec;
    return fs::is_regular_file(p, ec) && !isRemoved(p);
}

std::string Store::partPath(Slot slot, const std::string& id, const std::string& suffix) const {
    const std::string name = cacheFileName(id, suffix);
    if (name.empty()) return {};
    std::error_code ec;
    fs::create_directories(toPath(dir(slot)), ec);
    return toUtf8(toPath(dir(slot)) / toPath(name + kPartSuffix));
}

bool Store::commit(Slot slot, const std::string& id, const std::string& suffix, std::string& error) const {
    const std::string name = cacheFileName(id, suffix);
    const fs::path part = toPath(dir(slot)) / toPath(name + kPartSuffix);
    const fs::path dest = toPath(dir(slot)) / toPath(name);
    std::error_code ec;
    std::string head(4096, '\0');
    {
        std::ifstream in(part, std::ios::binary);
        in.read(head.data(), static_cast<std::streamsize>(head.size()));
        head.resize(static_cast<std::size_t>(in.gcount()));
    }
    const std::string serverError = serverErrorInBody(head);
    if (!serverError.empty()) {
        error = serverError;
        fs::remove(part, ec);
        return false;
    }
    fs::rename(part, dest, ec);
    if (ec) {
        error = ec.message();
        fs::remove(part, ec);
        return false;
    }
    fs::remove(fs::path(dest).concat(kRemovedSuffix), ec);
    return true;
}

void Store::discardPart(Slot slot, const std::string& id, const std::string& suffix) const {
    const std::string name = cacheFileName(id, suffix);
    if (name.empty()) return;
    std::error_code ec;
    fs::remove(toPath(dir(slot)) / toPath(name + kPartSuffix), ec);
}

std::size_t Store::removePinned(const std::string& id, std::size_t* deferred) const {
    const std::string stem = sanitizeFileName(id) + ".";
    const fs::path d = toPath(dir(Slot::Pinned));
    std::size_t removed = 0;
    for (const auto& f : listFiles(d)) {
        if (f.name.compare(0, stem.size(), stem) != 0) continue;
        const fs::path p = d / toPath(f.name);
        std::error_code ec;
        if (fs::remove(p, ec)) { ++removed; continue; }
        std::ofstream marker(fs::path(p).concat(kRemovedSuffix), std::ios::binary);
        if (!marker) continue;
        ++removed;
        if (deferred) ++*deferred;
    }
    return removed;
}

std::size_t Store::purgeRemoved(std::size_t* pending) const {
    const fs::path d = toPath(dir(Slot::Pinned));
    std::vector<fs::path> markers;
    std::error_code ec;
    for (fs::directory_iterator it(d, ec), end; !ec && it != end; it.increment(ec))
        if (endsWith(toUtf8(it->path().filename()), kRemovedSuffix)) markers.push_back(it->path());
    std::size_t purged = 0;
    for (const auto& marker : markers) {
        fs::path target = marker;
        target.replace_extension();
        std::error_code rec;
        if (fs::exists(target, rec) && !fs::remove(target, rec)) {
            if (pending) ++*pending;
            continue;
        }
        fs::remove(marker, rec);
        ++purged;
    }
    return purged;
}

std::size_t Store::evictPrefetch(std::uint64_t limitBytes) const {
    const fs::path d = toPath(dir(Slot::Prefetch));
    std::size_t removed = 0;
    for (const auto& name : evictionPlan(listFiles(d), limitBytes)) {
        std::error_code ec;
        if (fs::remove(d / toPath(name), ec)) ++removed;
    }
    return removed;
}

std::size_t Store::clearPrefetch() const {
    const fs::path d = toPath(dir(Slot::Prefetch));
    std::size_t removed = 0;
    std::error_code ec;
    for (fs::directory_iterator it(d, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code rec;
        if (fs::remove(it->path(), rec)) ++removed;
    }
    return removed;
}

Usage Store::usage() const {
    Usage u;
    for (const auto& f : listFiles(toPath(dir(Slot::Pinned))))   { ++u.pinnedFiles;   u.pinnedBytes   += f.size; }
    for (const auto& f : listFiles(toPath(dir(Slot::Prefetch)))) { ++u.prefetchFiles; u.prefetchBytes += f.size; }
    return u;
}

}
