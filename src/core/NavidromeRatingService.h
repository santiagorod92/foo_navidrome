#pragma once
// Lets another component (e.g. a custom skin's own rating UI) set a Navidrome track's rating
// without going through foobar2000's standard file-tag pipeline (metadb_io_v2), which fails for
// navidrome:// paths with "Tagging of this file format is not supported" — there is no real file
// to tag. Implemented in main.cpp (SDK-only, every platform already compiles it), reusing the
// exact same setRatingOnServer()+syncRatingsToPlaylists() path the built-in Navidrome context-menu
// rating command already uses (see main.cpp's navidrome_context_menu).
//
// Query via service_enum_t<navidrome_rating_api>; zero enumerated instances when foo_navidrome
// isn't installed, so a caller must check before using it rather than assume presence.
#include <SDK/service.h>
#include <SDK/metadb.h>

namespace navidrome {

class NOVTABLE navidrome_rating_api : public service_base {
    FB2K_MAKE_SERVICE_INTERFACE_ENTRYPOINT(navidrome_rating_api);
public:
    // True if `track`'s path is a navidrome://track/... URI this service can rate.
    virtual bool is_navidrome_track(const metadb_handle_ptr& track) = 0;

    // Fire-and-forget: pushes the new rating to the server off the calling thread, then (on
    // success) updates NAVIDROME_RATING on every matching playlist entry so the UI reflects it —
    // same behaviour as the built-in Navidrome context-menu rating command. Preserves the
    // track's current starred state (only the rating changes). stars: 0..5 (0 clears). No-op if
    // is_navidrome_track(track) is false.
    virtual void set_rating_async(const metadb_handle_ptr& track, int stars) = 0;
};

// {DAD72066-852D-4787-9ECE-9E778F42DE08}
FOOGUIDDECL const GUID navidrome_rating_api::class_guid =
    { 0xdad72066, 0x852d, 0x4787, { 0x9e, 0xce, 0x9e, 0x77, 0x8f, 0x42, 0xde, 0x08 } };

} // namespace navidrome
