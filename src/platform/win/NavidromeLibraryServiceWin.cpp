#include "stdafx.h"
#include "SubsonicClientWin.h"
#include "../../core/MediaEnrichmentLogic.h"
#include "../../core/NavidromeLibraryPlatform.h"

bool navidrome::libraryIsConfigured() {
    return navidrome::SubsonicClientWin::get().isConfigured();
}

bool navidrome::libraryServerInfo(ServerInfo& out, std::string& outError) {
    return navidrome::SubsonicClientWin::get().serverInfo(out, outError);
}

std::vector<uint8_t> navidrome::libraryFetchCover(const std::string& id, int size, abort_callback& abort) {
    auto& client = navidrome::SubsonicClientWin::get();
    auto ctx = client.snapshot();
    const std::string key = size > 0 ? id + "@" + std::to_string(size) : id;
    auto cached = navidrome::CoverCache::instance().get(ctx.serverUrl, ctx.username, key);
    if (!cached.empty()) return std::vector<uint8_t>(cached.begin(), cached.end());

    auto result = client.httpGetBinary(ctx, client.coverArtURL(ctx, id, size), 20u * 1024 * 1024, abort);
    if (result.cls == navidrome::FetchClass::Aborted) throw exception_aborted();
    if (result.cls != navidrome::FetchClass::Ok) return {};
    navidrome::CoverCache::instance().put(ctx.serverUrl, ctx.username, key, result.body);
    return std::vector<uint8_t>(result.body.begin(), result.body.end());
}
