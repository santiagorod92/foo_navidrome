#import "stdafx.h"
#import "SubsonicClient.h"
#import "NavidromeDebugLog.h"
#include <SDK/album_art.h>

// ---------------------------------------------------------------------------
// Helper: extract a query-param value from a URL string
// ---------------------------------------------------------------------------
static pfc::string8 urlParamValue(const char* url, const char* key) {
    pfc::string8 token = key;
    token += "=";
    const char* pos = strstr(url, token.c_str());
    if (!pos) return "";
    pos += token.length();
    const char* end = strchr(pos, '&');
    if (!end) end = pos + strlen(pos);
    return pfc::string8(pos, (t_size)(end - pos));
}

// ---------------------------------------------------------------------------
// album_art_extractor_instance — fetches cover art for one track from Navidrome
// ---------------------------------------------------------------------------
class navidrome_art_instance : public album_art_extractor_instance {
public:
    navidrome_art_instance(const char* artId) : m_artId(artId) {}

    album_art_data_ptr query(const GUID& p_what, abort_callback& /*p_abort*/) override {
        if (p_what != album_art_ids::cover_front)
            throw exception_album_art_not_found();

        NSString *idStr = [NSString stringWithUTF8String:m_artId.c_str()];
        NSURL *url = [SubsonicClient.sharedClient coverArtURLForId:idStr size:0];
        if (!url) throw exception_album_art_not_found();

        NSError *err = nil;
        // Fetch through SubsonicClient so the configured custom headers (e.g.
        // Cloudflare Access tokens) are applied — a bare dataWithContentsOfURL:
        // would send none and get blocked behind a Zero Trust tunnel.
        NSData *data = [SubsonicClient.sharedClient dataForURL:url error:&err];
        if (!data || data.length == 0) {
            NAVIDROME_WARN("Art", std::string("no art for id=") + m_artId.c_str() +
                           (err ? std::string(" (") + (err.localizedDescription.UTF8String ?: "?") + ")" : ""));
            throw exception_album_art_not_found();
        }

        NAVIDROME_LOG("Art", std::string("art id=") + m_artId.c_str() + " -> " +
                      std::to_string((unsigned long)data.length) + " bytes");
        return album_art_data_impl::g_create(data.bytes, (t_size)data.length);
    }

private:
    pfc::string8 m_artId;
};

// ---------------------------------------------------------------------------
// album_art_extractor — foobar2000 calls is_our_path() for every track it
// needs art for. Returning true from is_our_path() guarantees open() is
// called, which is more reliable than album_art_fallback for HTTP streams.
// ---------------------------------------------------------------------------
class navidrome_art_extractor : public album_art_extractor {
public:
    bool is_our_path(const char* p_path, const char* /*p_ext*/) override {
        if (!p_path) return false;
        // New: navidrome:// URIs from the input handler.
        if (strncmp(p_path, "navidrome://", 12) == 0) return true;
        // Legacy: raw HTTP stream URLs (pre-URI-scheme playlists).
        return strstr(p_path, "/rest/stream.view") != nullptr;
    }

    album_art_extractor_instance_ptr open(file_ptr /*p_file*/,
                                          const char* p_path,
                                          abort_callback& /*p_abort*/) override {
        // Prefer the coverArt param (album / Folder.jpg ID embedded at enqueue
        // time). For navidrome:// URIs the coverArt is the album/folder art id
        // from Navidrome — same id used by getCoverArt.view, which on a tagged
        // library serves the embedded Folder.jpg / cover.jpg.
        //
        // Falls back to the song id ("id" or the path component after track/)
        // so Navidrome can still resolve via its per-track lookup.
        pfc::string8 artId = urlParamValue(p_path, "coverArt");
        if (artId.length() == 0)
            artId = urlParamValue(p_path, "id");
        if (artId.length() == 0) {
            // navidrome://track/<id>?... — extract <id> as last-resort
            const char *prefix = "navidrome://track/";
            const size_t prefixLen = 18;
            if (strncmp(p_path, prefix, prefixLen) == 0) {
                const char *idStart = p_path + prefixLen;
                const char *idEnd = strchr(idStart, '?');
                if (!idEnd) idEnd = idStart + strlen(idStart);
                if (idEnd > idStart) {
                    artId.set_string(idStart, (t_size)(idEnd - idStart));
                }
            }
        }
        if (artId.length() == 0) {
            NAVIDROME_WARN("Art", std::string("open: no art id resolvable from ") + p_path);
            throw exception_album_art_not_found();
        }
        return new service_impl_t<navidrome_art_instance>(artId.c_str());
    }
};

FB2K_SERVICE_FACTORY(navidrome_art_extractor);
