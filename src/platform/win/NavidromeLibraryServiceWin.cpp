// Windows half of navidrome_library_api (the service itself is shared, in main.cpp): the
// configured check and cover-art bytes. libraryClient() is BrowserWindow.cpp's WinBrowserClient.
#include "stdafx.h"
#include "SubsonicClientWin.h"
#include "../../core/MediaEnrichmentLogic.h"
#include "../../core/NavidromeLibraryPlatform.h"

bool navidrome::libraryIsConfigured() {
    return navidrome::SubsonicClientWin::get().isConfigured();
}

std::vector<uint8_t> navidrome::libraryFetchCover(const std::string& id, int size, abort_callback& abort) {
    auto& client = navidrome::SubsonicClientWin::get();
    auto ctx = client.snapshot();
    // Sized thumbnails are cached under their own key so they never shadow (or get shadowed
    // by) the original-size entry the album-art extractor stores under the bare id.
    const std::string key = size > 0 ? id + "@" + std::to_string(size) : id;
    auto cached = navidrome::CoverCache::instance().get(ctx.serverUrl, ctx.username, key);
    if (!cached.empty()) return std::vector<uint8_t>(cached.begin(), cached.end());

    auto result = client.httpGetBinary(ctx, client.coverArtURL(ctx, id, size), 20u * 1024 * 1024, abort);
    if (result.cls == navidrome::FetchClass::Aborted) throw exception_aborted();
    if (result.cls != navidrome::FetchClass::Ok) return {};
    navidrome::CoverCache::instance().put(ctx.serverUrl, ctx.username, key, result.body);
    return std::vector<uint8_t>(result.body.begin(), result.body.end());
}
