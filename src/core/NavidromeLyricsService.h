#pragma once
// Publishes a navidrome:// track's lyrics (synced when the server has timings) to other
// foobar2000 components, so e.g. a custom skin can show them — there's no file to read embedded
// lyrics from, and on macOS no ESLyric to fetch them. Same lookup and session cache as
// foo_navidrome's own lyrics panel.
//
// Query via service_enum_t<navidrome_lyrics_api>; zero enumerated instances when foo_navidrome
// isn't installed, so callers must check first.
//
// ABI: separate DLLs/CRTs — lines stream through a caller-implemented sink as plain
// const char*/int, nothing std::/pfc-owned crosses the boundary.
#include <SDK/service.h>
#include <SDK/metadb.h>
#include <SDK/abort_callback.h>

namespace navidrome {

class NOVTABLE lyrics_sink {
public:
    // Called once before any on_line(). `synced` = every line carries a start time.
    virtual void on_begin(bool synced, unsigned lineCount) = 0;
    // One line, in display (= time) order. `startMs` is -1 for unsynced lyrics; `text` is
    // UTF-8 and may be empty (an instrumental gap). Pointer valid only during the call.
    virtual void on_line(int startMs, const char* text) = 0;
};

class NOVTABLE navidrome_lyrics_api : public service_base {
    FB2K_MAKE_SERVICE_INTERFACE_ENTRYPOINT(navidrome_lyrics_api);
public:
    // True for a navidrome://track/<id> handle (the only kind get_lyrics() can serve).
    virtual bool is_navidrome_track(const metadb_handle_ptr& track) = 0;

    // BLOCKING (one request on a cache miss) — call from a worker thread, never the UI thread.
    // Returns true and streams the lines to `sink` when the server has lyrics. Returns false
    // when it has none (`errorOut` left empty) or the lookup failed (`errorOut` says why).
    // Throws exception_aborted if `abort` fires.
    virtual bool get_lyrics(const metadb_handle_ptr& track, lyrics_sink& sink,
                            abort_callback& abort, pfc::string_base& errorOut) = 0;
};

// {3E8B5C1D-7A42-4F69-B1D3-58C0E2A974F6}
FOOGUIDDECL const GUID navidrome_lyrics_api::class_guid =
    { 0x3e8b5c1d, 0x7a42, 0x4f69, { 0xb1, 0xd3, 0x58, 0xc0, 0xe2, 0xa9, 0x74, 0xf6 } };

} // namespace navidrome
