#pragma once
#include <SDK/service.h>
#include <SDK/album_art.h>
#include <SDK/abort_callback.h>

namespace navidrome {

class NOVTABLE library_album_sink {
public:
    virtual void on_album(const char* albumId, const char* albumName, const char* artistName,
                          const char* artistId, const char* coverArtId,
                          int year, int songCount) = 0;
};

class NOVTABLE navidrome_library_api : public service_base {
    FB2K_MAKE_SERVICE_INTERFACE_ENTRYPOINT(navidrome_library_api);
public:
    virtual bool is_configured() = 0;

    virtual bool list_albums(library_album_sink& sink, abort_callback& abort,
                             pfc::string_base& errorOut) = 0;

    virtual album_art_data_ptr fetch_cover(const char* coverArtId, int size, abort_callback& abort) = 0;

    virtual void play_album(const char* albumId, bool replace, bool play) = 0;

    virtual void play_artist(const char* artistId, bool replace, bool play) = 0;
};

FOOGUIDDECL const GUID navidrome_library_api::class_guid =
    { 0x6a0c8a21, 0x5b3e, 0x4c0f, { 0x9e, 0x77, 0x2d, 0x4f, 0x1b, 0x8c, 0x93, 0xa5 } };
}
