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

static contextmenu_group_popup_factory g_ctx_group(
    navidrome::guids::contextMenuGroup, contextmenu_groups::root, "Navidrome", 0.0);

constexpr unsigned kRatingItems = 6;
constexpr unsigned kItemStar    = 6;
constexpr unsigned kItemUnstar  = 7;

class navidrome_context_menu : public contextmenu_item_simple {
public:
    GUID get_parent() override { return navidrome::guids::contextMenuGroup; }

    unsigned get_num_items() override { return 8; }

    void get_item_name(unsigned index, pfc::string_base& out) override {
        if (index == kItemStar)   { out = navidrome::tr("Star");      return; }
        if (index == kItemUnstar) { out = navidrome::tr("Unstar");    return; }
        if (index == 0)           { out = navidrome::tr("No rating"); return; }
        pfc::string_formatter name;
        for (unsigned i = 0; i < index; ++i) name << "\xE2\x98\x85";
        out = name;
    }

    bool get_item_description(unsigned index, pfc::string_base& out) override {
        (void)index;
        out = navidrome::tr("Applies to the selected Navidrome tracks, server-side.");
        return true;
    }

    GUID get_item_guid(unsigned index) override {
        GUID g = navidrome::guids::contextMenuItemsBase;
        g.Data4[7] = static_cast<unsigned char>(0x01 + index);
        return g;
    }

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
        std::vector<navidrome::RatingUpdate> updates;
        for (t_size i = 0; i < data.get_count(); ++i) {
            navidrome::RatingUpdate u;
            u.songId = navidrome::trackIdFromURI(data[i]->get_path());
            if (u.songId.empty()) continue;

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
            navidrome::syncRatingsToPlaylists(std::move(done));
        }).detach();
    }
};

static contextmenu_item_factory_t<navidrome_context_menu> g_navidrome_context_menu;
}

static void bookmarkCurrentPosition() {
    metadb_handle_ptr track;
    auto pc = playback_control::get();
    if (!pc->get_now_playing(track) || track.is_empty()) {
        console::print(navidrome::tr("Navidrome: no track is currently playing"));
        return;
    }
    const std::string songId = navidrome::trackIdFromURI(track->get_path());
    if (songId.empty()) {
        console::print(navidrome::tr("Navidrome: current track isn't from Navidrome"));
        return;
    }
    const double positionMs = pc->playback_get_position() * 1000.0;
    std::thread([songId, positionMs]() {
        navidrome::dbg::runGuarded("Bookmark", "bookmarkCurrentPosition", [&] {
            std::string err;
            navidrome::libraryCore().createBookmark(songId, positionMs, "", err);
            if (!err.empty())
                console::print(navidrome::trf("Navidrome: failed to save bookmark: {0}", { err }).c_str());
            NAVIDROME_LOG("Bookmark", "save id=" + songId + " pos=" +
                std::to_string((long long)positionMs) + "ms" +
                (err.empty() ? " ok" : " FAILED: " + err));
        });
    }).detach();
}

class navidrome_file_mainmenu : public mainmenu_commands {
public:
    t_uint32 get_command_count() override { return 2; }
    GUID get_command(t_uint32 i) override {
        if (i == 0) return navidrome::guids::mainmenuOpenBrowser;
        if (i == 1) return navidrome::guids::mainmenuBookmark;
        throw pfc::exception_invalid_params();
    }
    void get_name(t_uint32 i, pfc::string_base& out) override {
        if (i == 0) { out = navidrome::tr("Open Navidrome Browser"); return; }
        if (i == 1) { out = navidrome::tr("Bookmark Current Position"); return; }
        throw pfc::exception_invalid_params();
    }
    bool get_description(t_uint32 i, pfc::string_base& out) override {
        if (i == 0) { out = navidrome::tr("Browse and stream music from your Navidrome server"); return true; }
        if (i == 1) { out = navidrome::tr("Save the current playback position to resume later"); return true; }
        return false;
    }
    GUID get_parent() override { return mainmenu_groups::file; }
    t_uint32 get_sort_priority() override { return 0xFF; }
    bool get_display(t_uint32 i, pfc::string_base& out, t_uint32& flags) override {
        get_name(i, out);
        flags = 0;
        return true;
    }
    void execute(t_uint32 i, service_ptr_t<service_base>) override {
        if (i == 0) { navidrome::showBrowserWindow(); return; }
        if (i == 1) { bookmarkCurrentPosition(); return; }
        throw pfc::exception_invalid_params();
    }
};

FB2K_SERVICE_FACTORY(navidrome_file_mainmenu);
