#pragma once
#include <SDK/service.h>
#include <SDK/album_art.h>
#include <SDK/abort_callback.h>
#include <SDK/metadb.h>

namespace navidrome {

class NOVTABLE library_album_sink {
public:
    virtual void on_album(const char* albumId, const char* albumName, const char* artistName,
                          const char* artistId, const char* coverArtId,
                          int year, int songCount) = 0;
};

class NOVTABLE library_album_sink_v2 {
public:
    virtual void on_album(const char* albumId, const char* albumName, const char* artistName,
                          const char* artistId, const char* coverArtId,
                          int year, int songCount, const char* created) = 0;
};

class NOVTABLE library_track_sink {
public:
    virtual void on_track(const metadb_handle_ptr& track) = 0;
};

class NOVTABLE artist_info_sink {
public:
    virtual void on_artist_info(const char* artistId, const char* biography, const char* imageUrl,
                                const char* lastFmUrl) = 0;
};

class NOVTABLE library_search_sink {
public:
    virtual void on_artist(const char* artistId, const char* name, const char* coverArtId, int albumCount) = 0;
    virtual void on_album(const char* albumId, const char* albumName, const char* artistName,
                          const char* artistId, const char* coverArtId,
                          int year, int songCount, const char* created) = 0;
    virtual void on_track(const metadb_handle_ptr& track) = 0;
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

class NOVTABLE navidrome_library_api_v2 : public navidrome_library_api {
    FB2K_MAKE_SERVICE_INTERFACE(navidrome_library_api_v2, navidrome_library_api);
public:
    virtual bool list_albums_v2(library_album_sink_v2& sink, abort_callback& abort,
                                pfc::string_base& errorOut) = 0;
};

class NOVTABLE navidrome_library_api_v3 : public navidrome_library_api_v2 {
    FB2K_MAKE_SERVICE_INTERFACE(navidrome_library_api_v3, navidrome_library_api_v2);
public:
    virtual bool list_tracks(library_track_sink& sink, abort_callback& abort,
                             pfc::string_base& errorOut) = 0;

    virtual bool artist_info(const char* artistName, artist_info_sink& sink, abort_callback& abort,
                             pfc::string_base& errorOut) = 0;
};

class NOVTABLE navidrome_library_api_v4 : public navidrome_library_api_v3 {
    FB2K_MAKE_SERVICE_INTERFACE(navidrome_library_api_v4, navidrome_library_api_v3);
public:
    virtual bool search(const char* query, library_search_sink& sink, abort_callback& abort,
                        pfc::string_base& errorOut) = 0;
};

FOOGUIDDECL const GUID navidrome_library_api::class_guid =
    { 0x6a0c8a21, 0x5b3e, 0x4c0f, { 0x9e, 0x77, 0x2d, 0x4f, 0x1b, 0x8c, 0x93, 0xa5 } };

FOOGUIDDECL const GUID navidrome_library_api_v2::class_guid =
    { 0x54ff3096, 0x126a, 0x4f90, { 0xb7, 0x36, 0xe2, 0xe2, 0x2d, 0xc2, 0x7e, 0x20 } };

FOOGUIDDECL const GUID navidrome_library_api_v3::class_guid =
    { 0x3c9d2b71, 0x8e4a, 0x4f6b, { 0x9a, 0x15, 0x6d, 0x2e, 0x47, 0xb8, 0x0c, 0x93 } };

FOOGUIDDECL const GUID navidrome_library_api_v4::class_guid =
    { 0xd5b15a67, 0x9044, 0x4417, { 0xa6, 0x51, 0xc9, 0x2c, 0xe9, 0x16, 0xd9, 0x76 } };
}
