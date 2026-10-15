#include "stdafx.h"
#include "SubsonicClientWin.h"
#include "../../core/MediaEnrichmentLogic.h"
#include "../../core/NavidromeLibraryPlatform.h"
#include <mutex>
#include <set>
#include <string>
#include <utility>

namespace {

    std::mutex g_coverDiagMutex;
    std::set<std::pair<navidrome::FetchClass, std::string>> g_coverDiagSeen;

    void logCoverError(navidrome::FetchClass cls, const std::string& id) {
        using namespace navidrome;
        if (cls == FetchClass::NotFound) return;

        {
            std::lock_guard<std::mutex> lock(g_coverDiagMutex);
            if (!g_coverDiagSeen.insert({cls, id}).second) return;
        }

        const char* msg = "";
        switch (cls) {
            case FetchClass::Auth:           msg = "Cover art fetch: authentication failed"; break;
            case FetchClass::ServerError:    msg = "Cover art fetch: server error"; break;
            case FetchClass::Transport:      msg = "Cover art fetch: network transport error"; break;
            case FetchClass::InvalidContent: msg = "Cover art fetch: invalid content"; break;
            default: break;
        }
        if (*msg) {
            console::print(msg);
        }
    }
}

navidrome::SubsonicCore& navidrome::libraryCore() {
    return navidrome::SubsonicClientWin::get().core();
}

std::vector<uint8_t> navidrome::libraryFetchCover(const std::string& id, int size, abort_callback& abort) {
    auto& client = navidrome::SubsonicClientWin::get();
    auto ctx = client.snapshot();
    const std::string key = size > 0 ? id + "@" + std::to_string(size) : id;
    auto cached = navidrome::CoverCache::instance().get(ctx.serverUrl, ctx.username, key);
    if (!cached.empty()) return std::vector<uint8_t>(cached.begin(), cached.end());

    auto result = client.httpGetBinary(ctx, client.coverArtURL(ctx, id, size), 20u * 1024 * 1024, abort);
    if (result.cls == navidrome::FetchClass::Aborted) throw exception_aborted();
    if (result.cls != navidrome::FetchClass::Ok) {
        logCoverError(result.cls, id);
        return {};
    }
    navidrome::CoverCache::instance().put(ctx.serverUrl, ctx.username, key, result.body);
    return std::vector<uint8_t>(result.body.begin(), result.body.end());
}
