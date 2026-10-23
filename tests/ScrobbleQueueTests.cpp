#include "TestHarness.h"
#include "../src/core/ScrobbleQueue.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

std::string tempQueuePath(const char* name) {
    auto dir = std::filesystem::temp_directory_path() / "foo_navidrome_scrobble_tests";
    std::filesystem::create_directories(dir);
    auto p = dir / name;
    std::filesystem::remove(p);
    return p.string();
}

navidrome::Error kindError(navidrome::ErrorKind kind, int code = 0) {
    navidrome::Error e;
    e.kind = kind;
    e.code = code;
    e.message = "x";
    return e;
}

}

TEST_CASE(testPendingScrobbleFormat) {
    std::vector<navidrome::PendingScrobble> items = { { "a", 1700000000000 }, { "b c", 5 } };
    auto back = navidrome::parsePendingScrobbles(navidrome::formatPendingScrobbles(items));
    check(back.size() == 2 && back[0].id == "a" && back[0].timeMs == 1700000000000 &&
          back[1].id == "b c" && back[1].timeMs == 5,
          "pending scrobbles round-trip through the file format");
    auto parsed = navidrome::parsePendingScrobbles("ok\t10\r\n\tnoid\nbad\tx\nneg\t-1\nnotab\n\nlast\t20");
    check(parsed.size() == 2 && parsed[0].id == "ok" && parsed[1].id == "last" && parsed[1].timeMs == 20,
          "malformed lines are skipped, CRLF and a missing final newline are fine");
}

TEST_CASE(testScrobbleQueueStore) {
    const std::string path = tempQueuePath("queue.tsv");
    {
        navidrome::ScrobbleQueue q(path);
        check(q.size() == 0 && q.front(10).empty(), "a missing file is an empty queue");
        q.add({ "a", 1 });
        q.add({ "b", 2 });
        q.add({ "c", 3 });
    }
    navidrome::ScrobbleQueue q(path);
    check(q.size() == 3, "the queue survives a restart");
    auto two = q.front(2);
    check(two.size() == 2 && two[0].id == "a" && two[1].id == "b", "front returns the oldest first");
    q.dropFront(2);
    check(q.size() == 1 && q.front(5)[0].id == "c", "dropFront removes the oldest");
    q.dropFront(9);
    check(q.size() == 0 && !std::filesystem::exists(path), "an emptied queue deletes its file");

    for (std::size_t i = 0; i < navidrome::kMaxPendingScrobbles + 3; ++i)
        q.add({ "id" + std::to_string(i), static_cast<std::int64_t>(i + 1) });
    check(q.size() == navidrome::kMaxPendingScrobbles && q.front(1)[0].id == "id3",
          "past the cap the oldest entries are dropped");
    q.dropFront(navidrome::kMaxPendingScrobbles);
}

TEST_CASE(testScrobbleQueuePolicy) {
    using navidrome::ErrorKind;
    check(navidrome::shouldQueueScrobble(kindError(ErrorKind::Network)) &&
          navidrome::shouldQueueScrobble(kindError(ErrorKind::Timeout)) &&
          navidrome::shouldQueueScrobble(kindError(ErrorKind::Auth, 40)) &&
          navidrome::shouldQueueScrobble(kindError(ErrorKind::NotFound)),
          "network, timeout, auth and an HTTP 404 keep the scrobble for later");
    check(!navidrome::shouldQueueScrobble(navidrome::Error{}) &&
          !navidrome::shouldQueueScrobble(kindError(ErrorKind::NotFound, 70)),
          "a sent scrobble or an unknown song id (code 70) is not queued");
}

TEST_CASE(testFlushPendingScrobbles) {
    using navidrome::ErrorKind;
    const std::string path = tempQueuePath("flush.tsv");
    {
        navidrome::ScrobbleQueue q(path);
        for (int i = 0; i < 120; ++i) q.add({ "s" + std::to_string(i), i + 1 });
        std::vector<std::size_t> sizes;
        auto r = navidrome::flushPendingScrobbles(q, [&](const std::vector<navidrome::PendingScrobble>& b) {
            sizes.push_back(b.size());
            return navidrome::Error{};
        });
        check(r.sent == 120 && r.remaining == 0 && r.stoppedBy.ok() &&
              sizes == std::vector<std::size_t>({ 50, 50, 20 }),
              "a reachable server takes the queue in batches of 50");
    }
    {
        navidrome::ScrobbleQueue q(path);
        for (int i = 0; i < 3; ++i) q.add({ "s" + std::to_string(i), i + 1 });
        int calls = 0;
        auto r = navidrome::flushPendingScrobbles(q, [&](const std::vector<navidrome::PendingScrobble>&) {
            ++calls;
            return kindError(ErrorKind::Network);
        });
        check(r.sent == 0 && r.remaining == 3 && calls == 1 && r.stoppedBy.kind == ErrorKind::Network,
              "an unreachable server stops the flush after one request and keeps everything");
    }
    {
        navidrome::ScrobbleQueue q(path);
        q.dropFront(99);
        q.add({ "good1", 1 });
        q.add({ "gone", 2 });
        q.add({ "good2", 3 });
        auto r = navidrome::flushPendingScrobbles(q, [&](const std::vector<navidrome::PendingScrobble>& b) {
            for (const auto& p : b)
                if (p.id == "gone") return kindError(ErrorKind::NotFound, 70);
            return navidrome::Error{};
        });
        check(r.sent == 2 && r.dropped == 1 && r.remaining == 0,
              "a batch rejected for an unknown id is resent one by one and only that id is dropped");
    }
    {
        navidrome::ScrobbleQueue q(path);
        q.add({ "gone", 1 });
        q.add({ "later", 2 });
        int calls = 0;
        auto r = navidrome::flushPendingScrobbles(q, [&](const std::vector<navidrome::PendingScrobble>& b) {
            ++calls;
            if (b.size() > 1 || b[0].id == "gone") return kindError(ErrorKind::NotFound, 70);
            return kindError(ErrorKind::Timeout);
        });
        check(r.dropped == 1 && r.remaining == 1 && q.front(1)[0].id == "later" &&
              r.stoppedBy.kind == ErrorKind::Timeout,
              "a network failure while resending one by one stops and keeps the rest");
        q.dropFront(9);
    }
}
