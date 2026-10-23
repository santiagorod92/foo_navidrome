#pragma once

#include "OfflineCache.h"
#include "SubsonicErrors.h"
#include "SubsonicModels.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace navidrome {

constexpr std::size_t kMaxPendingScrobbles = 2000;
constexpr std::size_t kScrobbleBatchSize   = 50;

inline std::vector<PendingScrobble> parsePendingScrobbles(const std::string& text) {
    std::vector<PendingScrobble> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto tab = line.find('\t');
        if (tab == std::string::npos || tab == 0) continue;
        const std::string time = line.substr(tab + 1);
        char* end = nullptr;
        const long long ms = std::strtoll(time.c_str(), &end, 10);
        if (time.empty() || *end != '\0' || ms <= 0) continue;
        out.push_back({ line.substr(0, tab), static_cast<std::int64_t>(ms) });
    }
    return out;
}

inline std::string formatPendingScrobbles(const std::vector<PendingScrobble>& items) {
    std::string out;
    for (const auto& p : items) out += p.id + "\t" + std::to_string(p.timeMs) + "\n";
    return out;
}

class ScrobbleQueue {
public:
    explicit ScrobbleQueue(std::string pathUtf8) : m_path(std::move(pathUtf8)) {}

    std::size_t add(const PendingScrobble& item) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto items = loadLocked();
        items.push_back(item);
        if (items.size() > kMaxPendingScrobbles)
            items.erase(items.begin(), items.end() - kMaxPendingScrobbles);
        saveLocked(items);
        return items.size();
    }

    std::vector<PendingScrobble> front(std::size_t n) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto items = loadLocked();
        if (items.size() > n) items.resize(n);
        return items;
    }

    void dropFront(std::size_t n) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto items = loadLocked();
        items.erase(items.begin(), items.begin() + static_cast<std::ptrdiff_t>((std::min)(n, items.size())));
        saveLocked(items);
    }

    std::size_t size() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return loadLocked().size();
    }

private:
    std::vector<PendingScrobble> loadLocked() const {
        std::ifstream in(offline::fsPath(m_path), std::ios::binary);
        if (!in) return {};
        std::ostringstream ss;
        ss << in.rdbuf();
        return parsePendingScrobbles(ss.str());
    }

    void saveLocked(const std::vector<PendingScrobble>& items) const {
        std::error_code ec;
        const auto path = offline::fsPath(m_path);
        if (items.empty()) { std::filesystem::remove(path, ec); return; }
        const auto tmp = offline::fsPath(m_path + ".tmp");
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            if (!out) return;
            out << formatPendingScrobbles(items);
            if (!out) return;
        }
        std::filesystem::rename(tmp, path, ec);
    }

    std::string        m_path;
    mutable std::mutex m_mutex;
};

struct ScrobbleFlushResult {
    std::size_t sent      = 0;
    std::size_t dropped   = 0;
    std::size_t remaining = 0;
    Error       stoppedBy;
};

inline bool scrobbleRejectedForGood(const Error& e) { return e.kind == ErrorKind::NotFound && e.code == 70; }

inline bool shouldQueueScrobble(const Error& e) { return !e.ok() && !scrobbleRejectedForGood(e); }

inline ScrobbleFlushResult flushPendingScrobbles(
        ScrobbleQueue& queue,
        const std::function<Error(const std::vector<PendingScrobble>&)>& send) {
    ScrobbleFlushResult r;
    while (r.stoppedBy.ok()) {
        const auto batch = queue.front(kScrobbleBatchSize);
        if (batch.empty()) break;
        Error e = send(batch);
        if (e.ok()) { r.sent += batch.size(); queue.dropFront(batch.size()); continue; }
        if (!scrobbleRejectedForGood(e)) { r.stoppedBy = e; break; }
        for (const auto& item : batch) {
            Error one = batch.size() == 1 ? e : send({ item });
            if (!one.ok() && !scrobbleRejectedForGood(one)) { r.stoppedBy = one; break; }
            if (one.ok()) ++r.sent; else ++r.dropped;
            queue.dropFront(1);
        }
    }
    r.remaining = queue.size();
    return r;
}

}
