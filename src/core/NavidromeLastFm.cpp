#include "stdafx.h"
#include "NavidromeLastFm.h"
#include "LastFm.h"
#include "NavidromeLibraryPlatform.h"
#include "NavidromeDebugLog.h"
#include "NavidromeSettings.h"
#include "I18n.h"
#include <SDK/http_client.h>

namespace {

std::string lastFmGet(const std::string& url) {
    try {
        abort_callback_dummy abort;
        http_request::ptr req = http_client::get()->create_request("GET");
        file::ptr in = req->run(url.c_str(), abort);
        std::string body;
        char buf[16 * 1024];
        for (;;) {
            const t_size n = in->read(buf, sizeof(buf), abort);
            if (n == 0) break;
            body.append(buf, n);
            if (body.size() > 1024 * 1024) break;
        }
        return body;
    } catch (const std::exception& e) {
        NAVIDROME_WARN("API", std::string("Last.fm artist.getinfo failed: ") + e.what());
        return {};
    }
}

}

navidrome::ArtistInfo navidrome::artistInfoForDisplay(const std::string& artistId,
                                                      const std::string& artistName,
                                                      std::string& outError) {
    ArtistInfo info = libraryClient().getArtistInfo(artistId, outError);
    if (!outError.empty() || !stripHtmlTags(info.biography).empty()) return info;
    const std::string apiKey = lastfm::trimSpaces(cfg_lastfm_api_key.get().c_str());
    if (apiKey.empty()) {
        NAVIDROME_LOG("API", "no server biography for \"" + artistName + "\"; no Last.fm API key set");
        return info;
    }
    NAVIDROME_LOG("API", "no server biography for \"" + artistName + "\", asking Last.fm");
    ArtistInfo out = lastfm::withBiographyFallback(
        info, artistName, apiKey, i18n::languageCode(i18n::language()),
        [&](const std::string& url) {
            NAVIDROME_LOG("API", "Last.fm artist.getinfo for \"" + artistName + "\"");
            return lastFmGet(url);
        });
    NAVIDROME_LOG("API", out.biography.empty()
        ? "Last.fm has no biography for \"" + artistName + "\" either"
        : "Last.fm biography found for \"" + artistName + "\" at " + out.lastFmUrl);
    return out;
}
