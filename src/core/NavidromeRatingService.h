#pragma once
#include <SDK/service.h>
#include <SDK/metadb.h>

namespace navidrome {

class NOVTABLE navidrome_rating_api : public service_base {
    FB2K_MAKE_SERVICE_INTERFACE_ENTRYPOINT(navidrome_rating_api);
public:
    virtual bool is_navidrome_track(const metadb_handle_ptr& track) = 0;

    virtual void set_rating_async(const metadb_handle_ptr& track, int stars) = 0;
};

FOOGUIDDECL const GUID navidrome_rating_api::class_guid =
    { 0xdad72066, 0x852d, 0x4787, { 0x9e, 0xce, 0x9e, 0x77, 0x8f, 0x42, 0xde, 0x08 } };
}
