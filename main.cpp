#include "stdafx.h"
#include "NavidromePlaylistSync.h"
#include "NavidromeBrowserEnqueue.h"
#include "SubsonicTypes.h"
#include <SDK/metadb.h>
#include <SDK/playlist.h>
#include <SDK/playable_location.h>
#include <SDK/playback_control.h>
#include <SDK/advconfig.h>
#include <SDK/contextmenu.h>
#include <helpers/advconfig_impl.h>
#include <chrono>
#include <unordered_map>
#include <unordered_set>
#include <cstring>
#include <cstdlib>
#include <thread>
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

// ---------------------------------------------------------------------------
// Browser enqueue — see NavidromeBrowserEnqueue.h. SDK-only, so it isn't
// written once per platform.
// ---------------------------------------------------------------------------

void navidrome::seekWhenReady(double positionSeconds) {
    std::thread([positionSeconds]() {
        auto pc = playback_control::get();
        for (int i = 0; i < 30; ++i) {
            if (pc->is_playing() && pc->playback_can_seek()) {
                fb2k::inMainThread([positionSeconds]() {
                    playback_control::get()->playback_seek(positionSeconds);
                });
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }).detach();
}

std::size_t navidrome::enqueueBrowserNodes(
        const std::vector<BrowserNodePtr>& nodes,
        bool play,
        bool clearFirst,
        const std::function<std::string(const std::string&)>& radioUrl,
        std::string& statusOut) {
    if (nodes.empty()) { statusOut = "No songs selected"; return 0; }

    metadb_handle_list tracks;
    auto hints = metadb_io_v2::get()->create_hint_list();

    for (const auto& node : nodes) {
        if (!node) continue;
        metadb_handle_ptr handle;
        playable_location_impl loc;

        if (node->type == BrowserNode::Radio) {
            // Raw stream URL — bypasses navidrome:// entirely; foobar's stock
            // HTTP input plays it (and any Shoutcast/Icecast metadata) with no
            // involvement from our input handler.
            const std::string url = radioUrl ? radioUrl(node->id) : std::string();
            if (url.empty()) continue;
            loc.set_path(url.c_str());
            loc.set_subsong(0);
            metadb::get()->handle_create(handle, loc);
            tracks += handle;

            file_info_impl info;
            if (!node->displayName.empty()) info.meta_set("title", node->displayName.c_str());
            hints->add_hint(handle, info, filestats_invalid, true);
            continue;
        }

        navidrome::TrackURI t;
        t.id         = node->id;
        t.title      = node->displayName;
        t.artist     = node->subtitle;
        t.album      = node->album;
        t.coverArtId = node->coverArtId;
        t.suffix     = node->suffix;
        t.albumId    = node->albumId;
        t.track      = node->track;
        t.year       = node->year;
        t.rating     = node->rating;
        t.duration   = node->duration;
        t.starred    = node->starred;
        const std::string uri = navidrome::buildTrackURI(t);
        if (uri.empty()) continue;

        loc.set_path(uri.c_str());
        loc.set_subsong(0);
        metadb::get()->handle_create(handle, loc);
        tracks += handle;

        file_info_impl info;
        if (!node->displayName.empty()) info.meta_set("title",  node->displayName.c_str());
        if (!node->subtitle.empty())    info.meta_set("artist", node->subtitle.c_str());
        if (!node->album.empty())       info.meta_set("album",  node->album.c_str());
        if (node->track > 0)            info.meta_set("tracknumber", pfc::format_int(node->track));
        if (node->year > 0)             info.meta_set("date",   pfc::format_int(node->year));
        if (node->duration > 0)         info.set_length(node->duration);
        // The hint pre-populates metadb, so get_info() is not called for a
        // freshly enqueued track — the rating has to be set here too or the
        // column stays empty until an info reload.
        if (node->rating > 0)           info.meta_set(navidrome::kRatingTag, pfc::format_int(node->rating));
        if (node->starred)              info.meta_set(navidrome::kStarredTag, "1");
        hints->add_hint(handle, info, filestats_invalid, true);
    }
    hints->on_done();

    auto pm = playlist_manager::get();
    t_size pl = pm->get_active_playlist();
    if (pl == pfc_infinite) {
        pm->create_playlist("Navidrome", ~0, pfc_infinite);
        pl = pm->get_active_playlist();
    }
    if (clearFirst) pm->playlist_clear(pl);
    t_size insertPos = pm->playlist_get_item_count(pl);
    pm->playlist_add_items(pl, tracks, pfc::bit_array_false());

    if (play && tracks.get_count() > 0) {
        // Start playback honoring the user's Playback > Order setting (Shuffle,
        // Random, Default, ...). track_command_play asks the active playback
        // order for the starting track; the focus biases in-order modes to the
        // first newly-added track. (playlist_execute_default_action would
        // instead pin that exact track and ignore the order.)
        pm->set_active_playlist(pl);
        pm->set_playing_playlist(pl);
        pm->playlist_set_focus_item(pl, insertPos);
        playback_control::get()->start(playback_control::track_command_play);

        // Resume a saved position when this was a single bookmarked song.
        if (nodes.size() == 1 && nodes[0] && nodes[0]->bookmarkPositionMs > 0)
            navidrome::seekWhenReady(nodes[0]->bookmarkPositionMs / 1000.0);
    }

    statusOut = "Added " + std::to_string(tracks.get_count()) + " tracks";
    return tracks.get_count();
}

// ---------------------------------------------------------------------------
// Playlist context menu — rate / star without going back to the browser tree.
// Pure SDK apart from the two client calls, so it isn't written twice. Hides
// itself when nothing in the selection is one of ours.
// ---------------------------------------------------------------------------

namespace {

static constexpr GUID guid_ctx_group =
    { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x03, 0x00 } };

static contextmenu_group_popup_factory g_ctx_group(
    guid_ctx_group, contextmenu_groups::root, "Navidrome", 0.0);

// 0-5 set that rating (0 clears it), 6 stars, 7 unstars.
constexpr unsigned kRatingItems = 6;
constexpr unsigned kItemStar    = 6;
constexpr unsigned kItemUnstar  = 7;

class navidrome_context_menu : public contextmenu_item_simple {
public:
    GUID get_parent() override { return guid_ctx_group; }

    unsigned get_num_items() override { return 8; }

    void get_item_name(unsigned index, pfc::string_base& out) override {
        if (index == kItemStar)   { out = "Star";      return; }
        if (index == kItemUnstar) { out = "Unstar";    return; }
        if (index == 0)           { out = "No rating"; return; }
        // No "Rating:" prefix — the submenu is already called Navidrome and the
        // stars say the rest.
        pfc::string_formatter name;
        for (unsigned i = 0; i < index; ++i) name << "\xE2\x98\x85";   // ★
        out = name;
    }

    bool get_item_description(unsigned index, pfc::string_base& out) override {
        (void)index;
        out = "Applies to the selected Navidrome tracks, server-side.";
        return true;
    }

    GUID get_item_guid(unsigned index) override {
        GUID g = { 0xa1b2c3d4, 0x1111, 0x2222,
                   { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x03, 0x01 } };
        g.Data4[7] = static_cast<unsigned char>(0x01 + index);
        return g;
    }

    // Returning false hides the item, so a playlist of local files never shows
    // a Navidrome submenu.
    bool context_get_display(unsigned index, metadb_handle_list_cref data,
                             pfc::string_base& out, unsigned& flags,
                             const GUID& caller) override {
        (void)flags; (void)caller;
        bool anyOurs = false;
        for (t_size i = 0; i < data.get_count() && !anyOurs; ++i)
            anyOurs = !navidrome::trackIdFromURI(data[i]->get_path()).empty();
        if (!anyOurs) return false;
        get_item_name(index, out);
        return true;
    }

    void context_command(unsigned index, metadb_handle_list_cref data,
                         const GUID& caller) override {
        (void)caller;
        // Seed from what the entries currently show, then change only the field
        // this command is about — setRating must not clear a star, and vice
        // versa. Reading metadb is main-thread work, so it happens here rather
        // than in the worker.
        std::vector<navidrome::RatingUpdate> updates;
        for (t_size i = 0; i < data.get_count(); ++i) {
            navidrome::RatingUpdate u;
            u.songId = navidrome::trackIdFromURI(data[i]->get_path());
            if (u.songId.empty()) continue;   // local file in a mixed playlist

            file_info_impl info;
            if (data[i]->get_info(info)) {
                if (info.meta_get_count_by_name(navidrome::kRatingTag) > 0)
                    u.rating = atoi(info.meta_get(navidrome::kRatingTag, 0));
                u.starred = info.meta_get_count_by_name(navidrome::kStarredTag) > 0;
            }

            if (index < kRatingItems) u.rating  = static_cast<int>(index);
            else                      u.starred = (index == kItemStar);
            updates.push_back(std::move(u));
        }
        if (updates.empty()) return;

        const bool rating = index < kRatingItems;
        std::thread([updates = std::move(updates), rating]() mutable {
            std::vector<navidrome::RatingUpdate> done;
            for (auto& u : updates) {
                const bool ok = rating ? navidrome::setRatingOnServer(u.songId, u.rating)
                                       : navidrome::setStarredOnServer(u.songId, u.starred);
                if (ok) done.push_back(std::move(u));
            }
            // Only what the server accepted reaches the playlist, so a failed
            // call leaves the old value visible instead of a hopeful one.
            navidrome::syncRatingsToPlaylists(std::move(done));
        }).detach();
    }
};

static contextmenu_item_factory_t<navidrome_context_menu> g_navidrome_context_menu;

} // namespace
