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

constexpr const char* kPlayQueuePlaylist = navidrome::N_("Navidrome Play Queue");

void runPlayQueueTask(const char* title, std::function<std::string(abort_callback&)> work,
                      std::function<void()> done) {
    struct State { std::string error; };
    auto state = std::make_shared<State>();
    auto cb = threaded_process_callback_lambda::create();
    cb->m_run = [state, work](threaded_process_status& status, abort_callback& abort) {
        status.set_item(navidrome::tr("Contacting the Navidrome server..."));
        try {
            state->error = work(abort);
        } catch (const exception_aborted&) {
            throw;
        } catch (const std::exception& e) {
            state->error = e.what();
        }
    };
    const std::string titleStr = title;
    cb->m_on_done = [state, done, titleStr](threaded_process_callback::ctx_t, bool aborted) {
        if (aborted) { NAVIDROME_LOG("PlayQueue", titleStr + ": aborted"); return; }
        if (!state->error.empty()) {
            NAVIDROME_WARN("PlayQueue", titleStr + " failed: " + state->error);
            navidrome::reportError(titleStr.c_str(), state->error);
            return;
        }
        done();
    };
    threaded_process::g_run_modeless(cb,
        threaded_process::flag_show_abort | threaded_process::flag_show_item |
        threaded_process::flag_show_delayed,
        core_api::get_main_window(), title);
}

void savePlayQueueToServer() {
    if (!navidrome::libraryCore().isConfigured()) {
        navidrome::reportError(navidrome::tr("Save Play Queue"), navidrome::tr("Set up the Navidrome server first: Preferences > Tools > Navidrome."));
        return;
    }
    auto pm = playlist_manager::get();
    auto pc = playback_control::get();
    t_size pl = pfc_infinite, current = pfc_infinite;
    double position = 0.0;
    if (pm->get_playing_item_location(&pl, &current)) {
        position = pc->playback_get_position();
    } else {
        pl = pm->get_active_playlist();
        if (pl == pfc_infinite) { navidrome::reportError(navidrome::tr("Save Play Queue"), navidrome::tr("No playlist is active.")); return; }
        current = pm->playlist_get_focus_item(pl);
    }
    metadb_handle_list items;
    pm->playlist_get_all_items(pl, items);
    std::vector<std::string> paths;
    paths.reserve(items.get_count());
    for (t_size i = 0; i < items.get_count(); ++i) paths.emplace_back(items[i]->get_path());

    const navidrome::PlayQueueSavePlan plan = navidrome::planPlayQueueSave(
        paths, current == pfc_infinite ? navidrome::kNoCurrentTrack : current, position);
    if (plan.songIds.empty()) {
        navidrome::reportError(navidrome::tr("Save Play Queue"), navidrome::tr("The playlist has no Navidrome tracks to save."));
        return;
    }
    NAVIDROME_LOG("PlayQueue", "save: " + std::to_string(plan.songIds.size()) + " ids, current=" +
                  plan.currentId + " positionMs=" + std::to_string(static_cast<long long>(plan.positionMs)) +
                  " truncated=" + std::to_string(plan.truncated) +
                  " foreign=" + std::to_string(plan.skippedForeign));
    runPlayQueueTask(navidrome::tr("Save Play Queue"),
        [plan](abort_callback&) {
            std::string err;
            if (navidrome::libraryClient().savePlayQueue(plan.songIds, plan.currentId, plan.positionMs, err))
                return std::string();
            return err.empty() ? std::string(navidrome::tr("The server rejected the play queue.")) : err;
        },
        [plan]() {
            const std::string msg = navidrome::playQueueSaveSummary(plan);
            console::printf("Navidrome: %s", msg.c_str());
            navidrome::showConfirmation(navidrome::tr("Save Play Queue"), msg);
        });
}

void restorePlayQueueFromServer() {
    if (!navidrome::libraryCore().isConfigured()) {
        navidrome::reportError(navidrome::tr("Restore Play Queue"), navidrome::tr("Set up the Navidrome server first: Preferences > Tools > Navidrome."));
        return;
    }
    auto plan = std::make_shared<navidrome::PlayQueueRestorePlan>();
    auto changedBy = std::make_shared<std::string>();
    runPlayQueueTask(navidrome::tr("Restore Play Queue"),
        [plan, changedBy](abort_callback& abort) {
            navidrome::PlayQueue queue;
            std::string err;
            if (!navidrome::libraryClient().getPlayQueue(queue, err))
                return err.empty() ? std::string(navidrome::tr("Could not read the play queue.")) : err;
            abort.check();
            *plan = navidrome::planPlayQueueRestore(queue);
            *changedBy = queue.changedBy;
            if (plan->nodes.empty()) return std::string(navidrome::tr("The server has no saved play queue."));
            return std::string();
        },
        [plan, changedBy]() {
            const metadb_handle_list tracks = navidrome::makeTrackHandles(plan->nodes, nullptr);
            if (tracks.get_count() == 0) return;
            auto pm = playlist_manager::get();
            t_size pl = pm->find_playlist(navidrome::tr(kPlayQueuePlaylist));
            if (pl == pfc_infinite) pl = pm->create_playlist(navidrome::tr(kPlayQueuePlaylist), pfc_infinite, pfc_infinite);
            if (pl == pfc_infinite) return;
            pm->playlist_clear(pl);
            pm->playlist_add_items(pl, tracks, pfc::bit_array_false());
            const t_size start = (std::min)(static_cast<t_size>(plan->startIndex), tracks.get_count() - 1);
            navidrome::playFrom(pl, start);
            if (plan->positionSeconds > 0) navidrome::seekWhenReady(plan->positionSeconds);
            NAVIDROME_LOG("PlayQueue", "restored " + std::to_string(tracks.get_count()) + " tracks, start=" +
                          std::to_string(start) + " position=" + std::to_string(plan->positionSeconds) +
                          (changedBy->empty() ? std::string() : " changedBy=" + *changedBy));
        });
}

class navidrome_playqueue_mainmenu : public mainmenu_commands {
public:
    t_uint32 get_command_count() override { return 2; }
    GUID get_command(t_uint32 i) override {
        if (i == 0) return navidrome::guids::mainmenuPlayQueueSave;
        if (i == 1) return navidrome::guids::mainmenuPlayQueueRestore;
        throw pfc::exception_invalid_params();
    }
    void get_name(t_uint32 i, pfc::string_base& out) override {
        if (i == 0) { out = navidrome::tr("Save Play Queue to Navidrome"); return; }
        if (i == 1) { out = navidrome::tr("Restore Play Queue from Navidrome"); return; }
        throw pfc::exception_invalid_params();
    }
    bool get_description(t_uint32 i, pfc::string_base& out) override {
        if (i == 0) { out = navidrome::tr("Save the playing (or active) playlist and position as your Navidrome play queue"); return true; }
        if (i == 1) { out = navidrome::tr("Load your Navidrome play queue, saved by any Subsonic client, and resume it"); return true; }
        return false;
    }
    GUID get_parent() override { return mainmenu_groups::file; }
    void execute(t_uint32 i, service_ptr_t<service_base>) override {
        if (i == 0) savePlayQueueToServer();
        else if (i == 1) restorePlayQueueFromServer();
    }
};

FB2K_SERVICE_FACTORY(navidrome_playqueue_mainmenu);
}
