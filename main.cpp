#include "stdafx.h"
#include "NavidromePlaylistSync.h"
#include "SubsonicTypes.h"
#include <SDK/metadb.h>
#include <SDK/playlist.h>
#include <SDK/advconfig.h>
#include <helpers/advconfig_impl.h>
#include <unordered_map>
#include <unordered_set>
#include <cstring>
#if __has_include("version_generated.h")
#  include "version_generated.h"
#endif
#ifndef COMPONENT_VERSION
#  define COMPONENT_VERSION "1.0.0"
#endif

DECLARE_COMPONENT_VERSION(
    "Navidrome Subsonic Client",
    COMPONENT_VERSION,
    "Streams music from Navidrome (or any Subsonic-compatible server) via the Subsonic API.\n"
    "\n"
    "Configuration: Preferences > Tools > Navidrome\n"
    "Browse: File > Open Navidrome Browser\n"
    "\n"
    "https://www.navidrome.org/"
);

VALIDATE_COMPONENT_FILENAME("foo_navidrome.dll");

FOOBAR2000_IMPLEMENT_CFG_VAR_DOWNGRADE;

// ---------------------------------------------------------------------------
// Playlist rating sync — see NavidromePlaylistSync.h for the rationale.
// ---------------------------------------------------------------------------

// The startup refresh costs one request per distinct album, which can't be
// bounded in advance, so it needs an off switch. advconfig gives both platforms
// the checkbox for one line — a cfg_bool would mean two preference dialogs.
static constexpr GUID guid_advcfg_refresh_on_start =
    { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01, 0x0e } };

static advconfig_checkbox_factory cfg_refresh_ratings_on_start(
    "Navidrome: refresh ratings on startup",
    guid_advcfg_refresh_on_start, advconfig_branch::guid_branch_tools,
    0.0, true);

bool navidrome::refreshRatingsOnStartEnabled() {
    return cfg_refresh_ratings_on_start.get();
}

namespace {

// Writes one song's state into a file_info. Returns false when nothing changed,
// so the caller can skip the hint — an unchanged forced hint is a pointless
// metadb write and a pointless repaint. Absent rather than "0" is what unrated
// has to look like, so a custom column renders empty instead of a zero.
bool applyRatingFields(file_info& info, const navidrome::RatingUpdate& u) {
    auto have = [&info](const char* field) -> const char* {
        return info.meta_get_count_by_name(field) > 0 ? info.meta_get(field, 0) : nullptr;
    };
    auto put = [&info](const char* field, const char* value) {
        if (value) info.meta_set(field, value);
        else       info.meta_remove_field(field);
    };
    auto same = [](const char* a, const char* b) {
        return (a == nullptr) == (b == nullptr) && (a == nullptr || strcmp(a, b) == 0);
    };

    pfc::string8 rating;
    if (u.rating > 0) rating << u.rating;
    const char* wantRating  = rating.is_empty() ? nullptr : rating.c_str();
    const char* wantStarred = u.starred ? "1" : nullptr;

    if (same(have(navidrome::kRatingTag), wantRating) &&
        same(have(navidrome::kStarredTag), wantStarred)) return false;

    put(navidrome::kRatingTag,  wantRating);
    put(navidrome::kStarredTag, wantStarred);
    return true;
}

} // namespace

void navidrome::syncRatingsToPlaylists(std::vector<RatingUpdate> updates) {
    if (updates.empty()) return;

    std::unordered_map<std::string, RatingUpdate> bySongId;
    for (auto& u : updates) {
        if (!u.songId.empty()) bySongId[u.songId] = u;
    }
    if (bySongId.empty()) return;

    fb2k::inMainThread([bySongId = std::move(bySongId)] {
        auto pm = playlist_manager::get();
        auto hints = metadb_hint_list_v3::create();
        size_t touched = 0;

        const t_size playlistCount = pm->get_playlist_count();
        for (t_size pl = 0; pl < playlistCount; ++pl) {
            metadb_handle_list items;
            pm->playlist_get_all_items(pl, items);
            for (t_size i = 0; i < items.get_count(); ++i) {
                metadb_handle_ptr handle = items[i];
                const std::string songId = navidrome::trackIdFromURI(handle->get_path());
                if (songId.empty()) continue;   // not one of ours
                auto it = bySongId.find(songId);
                if (it == bySongId.end()) continue;

                file_info_impl info;
                if (!handle->get_info(info)) continue;
                if (!applyRatingFields(info, it->second)) continue;

                // Forced, because a normal hint is skipped when the file hasn't
                // changed by timestamp — and ours never does, it's a URI.
                hints->add_hint_forced(handle, info, filestats_invalid, true);
                ++touched;
            }
        }

        if (touched > 0) hints->on_done();
    });
}

navidrome::PlaylistAlbumScan navidrome::scanPlaylistAlbums() {
    PlaylistAlbumScan scan;
    std::unordered_set<std::string> seen;

    auto pm = playlist_manager::get();
    const t_size playlistCount = pm->get_playlist_count();
    for (t_size pl = 0; pl < playlistCount; ++pl) {
        metadb_handle_list items;
        pm->playlist_get_all_items(pl, items);
        for (t_size i = 0; i < items.get_count(); ++i) {
            const std::string path = items[i]->get_path();
            if (navidrome::trackIdFromURI(path).empty()) continue;   // not one of ours
            scan.entries++;

            const std::string albumId = navidrome::queryParamFromURI(path, "albumId");
            if (albumId.empty()) { scan.ungrouped++; continue; }  // written before albumId existed
            if (seen.insert(albumId).second) scan.albumIds.push_back(albumId);
        }
    }
    return scan;
}
