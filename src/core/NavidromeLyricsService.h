#pragma once
#include <SDK/service.h>
#include <SDK/metadb.h>
#include <SDK/abort_callback.h>

namespace navidrome {

class NOVTABLE lyrics_sink {
public:
    virtual void on_begin(bool synced, unsigned lineCount) = 0;
    virtual void on_line(int startMs, const char* text) = 0;
};

class NOVTABLE navidrome_lyrics_api : public service_base {
    FB2K_MAKE_SERVICE_INTERFACE_ENTRYPOINT(navidrome_lyrics_api);
public:
    virtual bool is_navidrome_track(const metadb_handle_ptr& track) = 0;

    virtual bool get_lyrics(const metadb_handle_ptr& track, lyrics_sink& sink,
                            abort_callback& abort, pfc::string_base& errorOut) = 0;
};

FOOGUIDDECL const GUID navidrome_lyrics_api::class_guid =
    { 0x3e8b5c1d, 0x7a42, 0x4f69, { 0xb1, 0xd3, 0x58, 0xc0, 0xe2, 0xa9, 0x74, 0xf6 } };
}
