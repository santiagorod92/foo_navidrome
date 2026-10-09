#import "../../core/stdafx.h"
#import "SubsonicClient.h"
#import "../../core/SubsonicTypes.h"
#import "../../core/NavidromeDebugLog.h"
#include <SDK/album_art.h>

class navidrome_art_instance : public album_art_extractor_instance {
public:
    navidrome_art_instance(const char* artId) : m_artId(artId) {}

    album_art_data_ptr query(const GUID& p_what, abort_callback& ) override {
        if (p_what != album_art_ids::cover_front)
            throw exception_album_art_not_found();

        NSString *idStr = [NSString stringWithUTF8String:m_artId.c_str()];
        NSURL *url = [SubsonicClient.sharedClient coverArtURLForId:idStr size:0];
        if (!url) throw exception_album_art_not_found();

        NSError *err = nil;
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

class navidrome_art_extractor : public album_art_extractor {
public:
    bool is_our_path(const char* p_path, const char* ) override {
        return navidrome::isNavidromeArtPath(p_path);
    }

    album_art_extractor_instance_ptr open(file_ptr ,
                                          const char* p_path,
                                          abort_callback& ) override {
        std::string artId = navidrome::resolveArtId(p_path ? p_path : "");
        if (artId.empty()) {
            NAVIDROME_WARN("Art", std::string("open: no art id resolvable from ")
                           + (p_path ? p_path : "(null)"));
            throw exception_album_art_not_found();
        }
        return new service_impl_t<navidrome_art_instance>(artId.c_str());
    }
};

FB2K_SERVICE_FACTORY(navidrome_art_extractor);
