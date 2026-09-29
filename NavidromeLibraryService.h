#pragma once
// Publishes the Navidrome server's music library (every album, plus cover art and a "play this
// album" action) to other foobar2000 components, so e.g. a custom skin's album browser can show
// what the server has even though none of it is in the local Media Library (navidrome:// tracks
// only become metadb entries once they're played or queued).
//
// Query via service_enum_t<navidrome_library_api>; zero enumerated instances when foo_navidrome
// isn't installed (or on a platform that doesn't implement it), so callers must check first.
//
// ABI: the two components are separate DLLs with their own CRTs, so nothing owned by std::/pfc
// containers crosses the boundary — albums stream through a caller-implemented sink as plain
// const char*/int, and cover bytes come back as an album_art_data service object.
#include <SDK/service.h>
#include <SDK/album_art.h>
#include <SDK/abort_callback.h>

namespace navidrome {

class NOVTABLE library_album_sink {
public:
    // Called once per album, on the thread that called list_albums(). Pointers are only valid
    // for the duration of the call.
    virtual void on_album(const char* albumId, const char* albumName, const char* artistName,
                          const char* artistId, const char* coverArtId,
                          int year, int songCount) = 0;
};

class NOVTABLE navidrome_library_api : public service_base {
    FB2K_MAKE_SERVICE_INTERFACE_ENTRYPOINT(navidrome_library_api);
public:
    // False until a server URL/user is configured in Preferences.
    virtual bool is_configured() = 0;

    // BLOCKING (one request per artist) — call from a worker thread, never the UI thread.
    // Streams every album of every active library to `sink`. Returns false on failure
    // (`errorOut` describes it); albums already delivered stay valid.
    virtual bool list_albums(library_album_sink& sink, abort_callback& abort,
                             pfc::string_base& errorOut) = 0;

    // BLOCKING — worker thread only. Cover art image bytes (jpeg/png) for a coverArtId taken
    // from on_album(); `size` is the max edge in pixels (0 = original). Throws
    // exception_album_art_not_found / exception_aborted on failure.
    virtual album_art_data_ptr fetch_cover(const char* coverArtId, int size, abort_callback& abort) = 0;

    // Fire-and-forget, any thread: fetches the album's tracks, then on the main thread puts them
    // in the active playlist (`replace` clears it first; otherwise appends) and, if `play`,
    // starts playback honoring Playback > Order.
    virtual void play_album(const char* albumId, bool replace, bool play) = 0;

    // Same as play_album for every album of an artist (the browser's Enter-on-artist behaviour:
    // clear the active playlist, add all their tracks, start playing). Appended last so consumers
    // built against an older copy of this header keep working.
    virtual void play_artist(const char* artistId, bool replace, bool play) = 0;
};

// {6A0C8A21-5B3E-4C0F-9E77-2D4F1B8C93A5}
FOOGUIDDECL const GUID navidrome_library_api::class_guid =
    { 0x6a0c8a21, 0x5b3e, 0x4c0f, { 0x9e, 0x77, 0x2d, 0x4f, 0x1b, 0x8c, 0x93, 0xa5 } };

} // namespace navidrome
