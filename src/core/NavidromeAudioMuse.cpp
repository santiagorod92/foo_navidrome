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

namespace navidrome {

cfg_string cfg_audiomuse_url(navidrome::guids::cfgAudioMuseUrl, "");
cfg_string cfg_audiomuse_token(navidrome::guids::cfgAudioMuseToken, "");
cfg_string cfg_audiomuse_server(navidrome::guids::cfgAudioMuseServer, "");
cfg_var_modern::cfg_int cfg_audiomuse_count(navidrome::guids::cfgAudioMuseCount, audiomuse::kDefaultCount);
}

navidrome::audiomuse::Settings navidrome::audioMuseSettings() {
    audiomuse::Settings s;
    s.url    = cfg_audiomuse_url.get().c_str();
    s.token  = cfg_audiomuse_token.get().c_str();
    s.server = cfg_audiomuse_server.get().c_str();
    s.count  = audiomuse::clampCount(static_cast<int>(cfg_audiomuse_count.get()));
    return s;
}

namespace {

using FetchFn = std::function<std::vector<navidrome::BrowserNodePtr>(
    threaded_process_status&, abort_callback&, std::string&)>;
using DoneFn = std::function<void(std::vector<navidrome::BrowserNodePtr>)>;

void runWithProgress(const char* title, std::string what, FetchFn fetch, DoneFn done) {
    struct State {
        std::vector<navidrome::BrowserNodePtr> nodes;
        std::string error;
    };
    auto state = std::make_shared<State>();
    auto cb = threaded_process_callback_lambda::create();
    cb->m_run = [state, fetch, what](threaded_process_status& status, abort_callback& abort) {
        status.set_item(what.c_str());
        try {
            state->nodes = fetch(status, abort, state->error);
        } catch (const exception_aborted&) {
            throw;
        } catch (const std::exception& e) {
            state->nodes.clear();
            state->error = e.what();
            NAVIDROME_WARN("AudioMuse", what + ": exception: " + e.what());
        }
    };
    const std::string titleStr = title;
    cb->m_on_done = [state, done, titleStr](threaded_process_callback::ctx_t, bool aborted) {
        if (aborted) { NAVIDROME_LOG("AudioMuse", titleStr + ": aborted"); return; }
        if (state->nodes.empty()) {
            navidrome::reportError(titleStr.c_str(), state->error.empty() ? "No playable songs found." : state->error);
            return;
        }
        done(std::move(state->nodes));
    };
    threaded_process::g_run_modeless(cb,
        threaded_process::flag_show_abort | threaded_process::flag_show_item |
        threaded_process::flag_show_progress | threaded_process::flag_show_delayed,
        core_api::get_main_window(), title);
}

std::vector<navidrome::BrowserNodePtr> resolveWithProgress(
        const std::vector<navidrome::audiomuse::Track>& tracks,
        threaded_process_status& status, abort_callback& abort, std::string& err) {
    std::vector<navidrome::BrowserNodePtr> out;
    std::size_t missing = 0;
    for (std::size_t i = 0; i < tracks.size(); ++i) {
        abort.check();
        status.set_progress(i, tracks.size());
        std::size_t unresolved = 0;
        auto one = navidrome::audiomuse::resolveTracks(navidrome::libraryClient(), { tracks[i] }, unresolved);
        missing += unresolved;
        for (auto& n : one) out.push_back(std::move(n));
    }
    if (missing > 0)
        console::printf("Navidrome: AudioMuse-AI: %u of %u songs not found on the Navidrome server",
                        (unsigned)missing, (unsigned)tracks.size());
    if (out.empty() && err.empty() && !tracks.empty())
        err = "None of the songs AudioMuse-AI returned exist on the Navidrome server.";
    return out;
}

void runAudioMuse(navidrome::audiomuse::Kind kind, std::string subject,
                  std::function<std::vector<navidrome::audiomuse::Track>(std::string&)> query) {
    namespace am = navidrome::audiomuse;
    if (!navidrome::audioMuseSettings().configured()) {
        navidrome::reportError("AudioMuse-AI", "Set the AudioMuse-AI server URL first:\n"
                    "Preferences > Tools > Navidrome > AudioMuse-AI.");
        return;
    }
    const std::string name = am::playlistName(kind, subject);
    runWithProgress(am::kindName(kind), am::kindName(kind) + std::string(": ") + subject,
        [query](threaded_process_status& status, abort_callback& abort, std::string& err) {
            auto tracks = query(err);
            abort.check();
            if (tracks.empty()) return std::vector<navidrome::BrowserNodePtr>();
            return resolveWithProgress(tracks, status, abort, err);
        },
        [name](std::vector<navidrome::BrowserNodePtr> nodes) {
            const std::size_t n = navidrome::playNodesInNewPlaylist(nodes, name);
            NAVIDROME_LOG("AudioMuse", "playlist \"" + name + "\": " + std::to_string(n) + " tracks");
        });
}
}

void navidrome::audioMuseTextSearchPrompt() {
    static std::string last;
    std::string q = last;
    if (!promptForText("AudioMuse-AI Text Search", "Describe the music (e.g. \"calm piano with rain\"):", q))
        return;
    if (q.empty()) return;
    last = q;
    runAudioMuse(audiomuse::Kind::TextSearch, q, [q](std::string& err) {
        return audiomuse::textSearch(audioMusePoster(), audioMuseSettings(), q, err);
    });
}

void navidrome::audioMuseInstantPlaylistPrompt() {
    static std::string last;
    std::string q = last;
    if (!promptForText("AudioMuse-AI Instant Playlist", "Ask for a playlist (e.g. \"90s road trip rock\"):", q))
        return;
    if (q.empty()) return;
    last = q;
    runAudioMuse(audiomuse::Kind::InstantPlaylist, q, [q](std::string& err) {
        return audiomuse::instantPlaylist(audioMusePoster(), audioMuseSettings(), q, err);
    });
}

void navidrome::audioMuseAlchemy(std::vector<audiomuse::AlchemySeed> seeds, std::string label) {
    runAudioMuse(audiomuse::Kind::Alchemy, label, [seeds = std::move(seeds)](std::string& err) {
        return audiomuse::alchemy(audioMusePoster(), audioMuseSettings(), seeds, err);
    });
}

namespace {

constexpr const char* kInstantMixPlaylist = "Instant Mix";

void playInstantMix(metadb_handle_ptr seed, const std::vector<navidrome::BrowserNodePtr>& similar) {
    auto pm = playlist_manager::get();
    const metadb_handle_list mix = navidrome::makeTrackHandles(similar, nullptr);
    if (mix.get_count() == 0) return;

    metadb_handle_ptr playing;
    auto pc = playback_control::get();
    const bool seedPlaying = seed.is_valid() && pc->is_playing() &&
                             pc->get_now_playing(playing) && playing == seed;

    t_size pl = pm->find_playlist(kInstantMixPlaylist);
    t_size playingPl = pfc_infinite, playingIdx = pfc_infinite;
    const bool playingFromMix = pl != pfc_infinite &&
        pm->get_playing_item_location(&playingPl, &playingIdx) && playingPl == pl;

    if (pl == pfc_infinite) {
        pl = pm->create_playlist(kInstantMixPlaylist, pfc_infinite, pfc_infinite);
        if (pl == pfc_infinite) return;
    }

    if (seedPlaying && playingFromMix) {
        pm->playlist_remove_items(pl, pfc::bit_array_not(pfc::bit_array_one(playingIdx)));
        pm->playlist_add_items(pl, mix, pfc::bit_array_false());
    } else {
        pm->playlist_clear(pl);
        metadb_handle_list all;
        if (seed.is_valid()) all += seed;
        all += mix;
        pm->playlist_add_items(pl, all, pfc::bit_array_false());
    }
    pm->set_active_playlist(pl);
    NAVIDROME_LOG("UI", "Instant Mix: " + std::to_string(mix.get_count()) + " similar tracks" +
                  (seed.is_valid() ? " after the seed" : "") + (seedPlaying ? " (seed playing)" : ""));

    if (seedPlaying) {
        pm->set_playing_playlist(pl);
        pm->playlist_set_focus_item(pl, 0);
        return;
    }
    pm->set_playing_playlist(pl);
    if (seed.is_valid()) pm->playlist_execute_default_action(pl, 0);
    else                 navidrome::playFrom(pl, 0);
}

std::vector<navidrome::BrowserNodePtr> audioMuseMix(const std::string& seedId,
                                                    navidrome::BrowserNode::Type seedType,
                                                    threaded_process_status& status,
                                                    abort_callback& abort, std::string& err) {
    namespace am = navidrome::audiomuse;
    const am::Settings settings = navidrome::audioMuseSettings();
    if (!settings.configured()) {
        NAVIDROME_LOG("UI", "Instant Mix: Navidrome returned nothing, AudioMuse-AI not configured");
        err = am::noSimilarMessage(am::MixFallback::NotConfigured, {});
        return {};
    }
    if (!am::canMixFrom(seedType)) {
        err = am::noSimilarMessage(am::MixFallback::Unsupported, {});
        return {};
    }
    std::string amErr;
    const auto tracks = am::similarTo(navidrome::audioMusePoster(), settings, seedId, seedType, amErr);
    abort.check();
    if (!amErr.empty()) {
        err = am::noSimilarMessage(am::MixFallback::Failed, amErr);
        return {};
    }
    auto nodes = navidrome::withoutSongId(resolveWithProgress(tracks, status, abort, amErr), seedId);
    if (nodes.empty()) {
        NAVIDROME_WARN("UI", "Instant Mix: AudioMuse-AI fallback found nothing for " + seedId);
        err = amErr.empty() ? am::noSimilarMessage(am::MixFallback::Empty, {})
                            : am::noSimilarMessage(am::MixFallback::Failed, amErr);
    }
    return nodes;
}

void runInstantMix(metadb_handle_ptr seed, std::string seedId, navidrome::BrowserNode::Type seedType,
                   std::string title) {
    const int count = navidrome::audiomuse::clampCount(static_cast<int>(navidrome::cfg_audiomuse_count.get()));
    NAVIDROME_LOG("UI", "Instant Mix: seed=" + seedId + " count=" + std::to_string(count));
    runWithProgress("Instant Mix", "Instant Mix: " + title,
        [seedId, seedType, count](threaded_process_status& status, abort_callback& abort, std::string& err) {
            auto nodes = navidrome::fetchSimilarSongs(navidrome::libraryClient(), seedId, count, err);
            abort.check();
            nodes = navidrome::withoutSongId(std::move(nodes), seedId);
            if (nodes.empty() && err.empty()) nodes = audioMuseMix(seedId, seedType, status, abort, err);
            return nodes;
        },
        [seed](std::vector<navidrome::BrowserNodePtr> nodes) { playInstantMix(seed, nodes); });
}

void startInstantMix(metadb_handle_list_cref data) {
    for (t_size i = 0; i < data.get_count(); ++i) {
        const std::string seedId = navidrome::trackIdFromURI(data[i]->get_path());
        if (seedId.empty()) continue;
        std::string title = seedId;
        file_info_impl info;
        if (data[i]->get_info(info) && info.meta_get_count_by_name("title") > 0)
            title = info.meta_get("title", 0);
        runInstantMix(data[i], seedId, navidrome::BrowserNode::Song, title);
        return;
    }
}
}

void navidrome::startInstantMix(const BrowserNodePtr& seed) {
    if (!seed || !isSimilarEligible(*seed)) return;
    metadb_handle_ptr seedHandle;
    if (seed->type == BrowserNode::Song) {
        const metadb_handle_list h = navidrome::makeTrackHandles({ seed }, nullptr);
        if (h.get_count() > 0) seedHandle = h[0];
    }
    runInstantMix(seedHandle, seed->id, seed->type, seed->displayName.empty() ? seed->id : seed->displayName);
}

namespace {

void startAlchemy(metadb_handle_list_cref data) {
    std::vector<navidrome::audiomuse::AlchemySeed> seeds;
    std::string label;
    for (t_size i = 0; i < data.get_count(); ++i) {
        navidrome::audiomuse::AlchemySeed s;
        s.id = navidrome::trackIdFromURI(data[i]->get_path());
        if (s.id.empty()) continue;
        if (label.empty()) {
            file_info_impl info;
            if (data[i]->get_info(info) && info.meta_get_count_by_name("title") > 0)
                label = info.meta_get("title", 0);
        }
        seeds.push_back(std::move(s));
    }
    if (seeds.empty()) return;
    if (seeds.size() > 1) label += " + " + std::to_string(seeds.size() - 1) + " more";
    navidrome::audioMuseAlchemy(std::move(seeds), std::move(label));
}

constexpr unsigned kItemInstantMix = 0;
constexpr unsigned kItemAlchemy    = 1;

class navidrome_mix_context_menu : public contextmenu_item_simple {
public:
    GUID get_parent() override { return navidrome::guids::contextMenuGroup; }
    unsigned get_num_items() override { return 2; }

    void get_item_name(unsigned index, pfc::string_base& out) override {
        out = index == kItemInstantMix ? "Instant Mix" : "Song Alchemy (AudioMuse-AI)";
    }

    bool get_item_description(unsigned index, pfc::string_base& out) override {
        out = index == kItemInstantMix
            ? "Inserts songs similar to this track after it and plays them."
            : "Blends the selected tracks into a new AudioMuse-AI playlist.";
        return true;
    }

    GUID get_item_guid(unsigned index) override {
        GUID g = navidrome::guids::contextMenuMixItemsBase;
        g.Data4[7] = static_cast<unsigned char>(0x10 + index);
        return g;
    }

    bool context_get_display(unsigned index, metadb_handle_list_cref data,
                             pfc::string_base& out, unsigned& flags, const GUID& caller) override {
        (void)flags; (void)caller;
        bool anyOurs = false;
        for (t_size i = 0; i < data.get_count() && !anyOurs; ++i)
            anyOurs = !navidrome::trackIdFromURI(data[i]->get_path()).empty();
        if (!anyOurs) return false;
        if (index == kItemAlchemy && !navidrome::audioMuseSettings().configured()) return false;
        get_item_name(index, out);
        return true;
    }

    void context_command(unsigned index, metadb_handle_list_cref data, const GUID& caller) override {
        (void)caller;
        if (index == kItemInstantMix) startInstantMix(data);
        else                          startAlchemy(data);
    }
};

static contextmenu_item_factory_t<navidrome_mix_context_menu> g_navidrome_mix_context_menu;

static mainmenu_group_popup_factory g_mainmenu_audiomuse_group(
    navidrome::guids::mainmenuAudioMuseGroup, mainmenu_groups::file, mainmenu_commands::sort_priority_dontcare,
    "AudioMuse-AI");

class navidrome_audiomuse_mainmenu : public mainmenu_commands {
public:
    t_uint32 get_command_count() override { return 2; }
    GUID get_command(t_uint32 i) override {
        if (i == 0) return navidrome::guids::mainmenuAudioMuseSearch;
        if (i == 1) return navidrome::guids::mainmenuAudioMusePlaylist;
        throw pfc::exception_invalid_params();
    }
    void get_name(t_uint32 i, pfc::string_base& out) override {
        if (i == 0) { out = "Text Search..."; return; }
        if (i == 1) { out = "Instant Playlist..."; return; }
        throw pfc::exception_invalid_params();
    }
    bool get_description(t_uint32 i, pfc::string_base& out) override {
        if (i == 0) { out = "Find songs on Navidrome that sound like a description (AudioMuse-AI CLAP search)"; return true; }
        if (i == 1) { out = "Have AudioMuse-AI's assistant build a playlist from a request"; return true; }
        return false;
    }
    GUID get_parent() override { return navidrome::guids::mainmenuAudioMuseGroup; }
    void execute(t_uint32 i, service_ptr_t<service_base>) override {
        if (i == 0) navidrome::audioMuseTextSearchPrompt();
        else if (i == 1) navidrome::audioMuseInstantPlaylistPrompt();
    }
};

FB2K_SERVICE_FACTORY(navidrome_audiomuse_mainmenu);
}
