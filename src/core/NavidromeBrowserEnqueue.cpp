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

metadb_handle_list navidrome::makeTrackHandles(
        const std::vector<navidrome::BrowserNodePtr>& nodes,
        const std::function<std::string(const std::string&)>& radioUrl) {
    metadb_handle_list tracks;
    auto hints = metadb_io_v2::get()->create_hint_list();

    for (const auto& node : nodes) {
        if (!node) continue;
        metadb_handle_ptr handle;
        playable_location_impl loc;

        if (node->type == BrowserNode::Radio) {
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

        const navidrome::TrackURI t = navidrome::trackURIFromNode(*node);
        const std::string uri = navidrome::buildTrackURI(t);
        if (uri.empty()) continue;

        loc.set_path(uri.c_str());
        loc.set_subsong(0);
        metadb::get()->handle_create(handle, loc);
        tracks += handle;

        file_info_impl info;
        navidrome::applyTrackInfo(t, info);
        hints->add_hint(handle, info, filestats_invalid, true);
    }
    hints->on_done();
    return tracks;
}

void navidrome::playFrom(t_size pl, t_size first) {
    auto pm = playlist_manager::get();
    pm->set_active_playlist(pl);
    pm->set_playing_playlist(pl);
    pm->playlist_set_focus_item(pl, first);
    if (pm->playlist_execute_default_action(pl, first)) return;
    NAVIDROME_WARN("Playlist", "playFrom: default action refused on playlist " + std::to_string(pl) +
                   " item " + std::to_string(first) + ", starting playback instead");
    playback_control::get()->start(playback_control::track_command_play);
}

void navidrome::showBrowserQueueError(const std::string& msg) {
    console::printf("Navidrome Browser: %s", msg.c_str());
    popup_message::g_show(msg.c_str(), tr("Navidrome Browser"), popup_message::icon_error);
}

std::size_t navidrome::enqueueBrowserNodes(
        const std::vector<BrowserNodePtr>& nodes,
        bool play,
        bool clearFirst,
        const std::function<std::string(const std::string&)>& radioUrl,
        std::string& statusOut) {
    if (nodes.empty()) { statusOut = tr("No songs selected"); return 0; }

    const metadb_handle_list tracks = makeTrackHandles(nodes, radioUrl);

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
        playFrom(pl, insertPos);

        if (nodes.size() == 1 && nodes[0] && nodes[0]->bookmarkPositionMs > 0)
            navidrome::seekWhenReady(nodes[0]->bookmarkPositionMs / 1000.0);
    }

    statusOut = trf("Added {0} tracks", { std::to_string(tracks.get_count()) });
    return tracks.get_count();
}

std::size_t navidrome::playNodesInNewPlaylist(const std::vector<BrowserNodePtr>& nodes,
                                              const std::string& name) {
    const metadb_handle_list tracks = makeTrackHandles(nodes, nullptr);
    if (tracks.get_count() == 0) return 0;
    auto pm = playlist_manager::get();
    const t_size pl = pm->create_playlist(name.c_str(), pfc_infinite, pfc_infinite);
    if (pl == pfc_infinite) return 0;
    pm->playlist_add_items(pl, tracks, pfc::bit_array_false());
    playFrom(pl, 0);
    return tracks.get_count();
}

void navidrome::showConfirmation(const char* title, const std::string& msg) {
#ifdef __APPLE__
    fb2k::popup_toast::ptr toast;
    if (fb2k::popup_toast::tryGet(toast)) {
        toast->show_toast(msg.c_str(), {});
        NAVIDROME_LOG("UI", std::string("confirmation: toast (") + title + ")");
        return;
    }
#endif
    popup_message_v3::query_t q;
    q.title      = title;
    q.msg        = msg.c_str();
    q.buttons    = popup_message_v3::buttonOK;
    q.defButton  = popup_message_v3::buttonOK;
    q.icon       = popup_message_v3::iconInformation;
    q.wndParent  = core_api::get_main_window();
    q.show();
    NAVIDROME_LOG("UI", std::string("confirmation: dialog (") + title + ")");
}

void navidrome::reportError(const char* title, const std::string& msg) {
    console::printf("Navidrome: %s: %s", title, msg.c_str());
    popup_message::g_show(msg.c_str(), title, popup_message::icon_error);
}
