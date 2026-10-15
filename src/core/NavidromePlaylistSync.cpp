#include "stdafx.h"
#include "NavidromeSdkGlue.h"
#include "NavidromePlaylistSync.h"
#include "NavidromeBrowserEnqueue.h"
#include "NavidromeAudioMuse.h"
#include "NavidromeLibraryPlatform.h"
#include "NavidromeDebugLog.h"
#include "NavidromeGuids.h"
#include "NavidromeDiagnostics.h"
#include "NavidromeTrackInput.h"
#include "SubsonicTypes.h"
#include "NavidromeSettings.h"
#include <SDK/cfg_var.h>
#include <SDK/http_client.h>
#include <SDK/metadb.h>
#include <SDK/playlist.h>
#include <SDK/playable_location.h>
#include <SDK/playback_control.h>
#include <SDK/advconfig.h>
#include <SDK/contextmenu.h>
#include <SDK/album_art_helpers.h>
#include <SDK/threaded_process.h>
#include <SDK/menu.h>
#include <SDK/popup_message.h>
#include <SDK/initquit.h>
#include <SDK/coreversion.h>
#include <helpers/advconfig_impl.h>
#include <atomic>
#include <chrono>
#include <unordered_map>
#include <unordered_set>
#include <cstring>
#include <cstdlib>
#include <mutex>
#include <thread>

namespace {

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
}

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
                if (songId.empty()) continue;
                auto it = bySongId.find(songId);
                if (it == bySongId.end()) continue;

                file_info_impl info;
                if (!handle->get_info(info)) continue;
                if (!applyRatingFields(info, it->second)) continue;

                hints->add_hint_forced(handle, info, filestats_invalid, true);
                ++touched;
            }
        }

        if (touched > 0) hints->on_done();
    });
}

navidrome::ActivePlaylistIds navidrome::collectActivePlaylistIds() {
    ActivePlaylistIds out;
    auto pm = playlist_manager::get();
    const t_size pl = pm->get_active_playlist();
    if (pl == pfc_infinite) return out;
    out.hasPlaylist = true;
    pfc::string8 name;
    pm->playlist_get_name(pl, name);
    out.name = name.is_empty() ? "foobar2000" : name.c_str();
    metadb_handle_list items;
    pm->playlist_get_all_items(pl, items);
    for (t_size i = 0; i < items.get_count(); ++i) {
        std::string id = trackIdFromURI(items[i]->get_path());
        if (id.empty()) { ++out.skipped; continue; }
        out.songIds.push_back(std::move(id));
    }
    return out;
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
            if (navidrome::trackIdFromURI(path).empty()) continue;
            scan.entries++;

            const std::string albumId = navidrome::queryParamFromURI(path, "albumId");
            if (albumId.empty()) { scan.ungrouped++; continue; }
            if (seen.insert(albumId).second) scan.albumIds.push_back(albumId);
        }
    }
    return scan;
}
