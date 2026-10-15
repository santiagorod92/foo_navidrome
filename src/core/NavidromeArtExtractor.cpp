#include "stdafx.h"
#include "NavidromeLibraryPlatform.h"
#include "NavidromeDebugLog.h"
#include "SubsonicTypes.h"
#include <SDK/album_art.h>
#include <SDK/album_art_helpers.h>
#include <string>

namespace {

class navidrome_art_instance : public album_art_extractor_instance_v2 {
public:
    explicit navidrome_art_instance(std::string artId) : m_artId(std::move(artId)) {}

    album_art_data_ptr query(const GUID& what, abort_callback& abort) override {
        if (what != album_art_ids::cover_front) throw exception_album_art_not_found();
        const auto bytes = navidrome::libraryFetchCover(m_artId, 0, abort);
        if (bytes.empty()) throw exception_album_art_not_found();
        NAVIDROME_LOG("Art", "art id=" + m_artId + " -> " + std::to_string(bytes.size()) + " bytes");
        return album_art_data_impl::g_create(bytes.data(), bytes.size());
    }

    album_art_path_list::ptr query_paths(const GUID&, abort_callback&) override {
        throw exception_album_art_not_found();
    }

private:
    std::string m_artId;
};

class navidrome_art_extractor : public album_art_extractor {
public:
    bool is_our_path(const char* path, const char*) override {
        return navidrome::isNavidromeArtPath(path);
    }

    album_art_extractor_instance_ptr open(file_ptr, const char* path, abort_callback&) override {
        std::string artId = navidrome::resolveArtId(path ? path : "");
        if (artId.empty()) {
            NAVIDROME_WARN("Art", std::string("open: no art id resolvable from ") + (path ? path : "(null)"));
            throw exception_album_art_not_found();
        }
        return fb2k::service_new<navidrome_art_instance>(std::move(artId));
    }
};

FB2K_SERVICE_FACTORY(navidrome_art_extractor);

}
