#include "stdafx.h"
#include "NavidromeOfflineCache.h"
#include "NavidromeSdkGlue.h"
#include "NavidromeLibraryPlatform.h"
#include "NavidromeDebugLog.h"
#include "NavidromeGuids.h"
#include "NavidromeSettings.h"
#include "SubsonicTypes.h"
#include <SDK/http_client.h>
#include <SDK/playlist.h>
#include <SDK/play_callback.h>
#include <SDK/contextmenu.h>
#include <SDK/threaded_process.h>
#include <SDK/initquit.h>
#include <SDK/advconfig.h>
#include <helpers/advconfig_impl.h>
#include <atomic>
#include <fstream>
#include <memory>
#include <mutex>
#include <thread>

static advconfig_checkbox_factory cfg_prefetch(
    "Navidrome: prefetch the next track to a local cache", "foo_navidrome.prefetch",
    navidrome::guids::advPrefetch, advconfig_branch::guid_branch_tools, 0.0, false);

static advconfig_integer_factory cfg_prefetch_limit(
    "Navidrome: prefetch cache size (MB)", "foo_navidrome.prefetch_limit_mb",
    navidrome::guids::advPrefetchLimit, advconfig_branch::guid_branch_tools, 0.0,
    navidrome::offline::kDefaultPrefetchLimitMB, 64, 1024 * 1024);

bool navidrome::prefetchEnabled() { return cfg_prefetch.get(); }

std::uint64_t navidrome::prefetchLimitMB() { return cfg_prefetch_limit.get(); }

navidrome::offline::Store& navidrome::offlineStore() {
    static offline::Store store(
        filesystem::g_get_native_path(core_api::pathInProfile("foo_navidrome-cache")).c_str());
    return store;
}

namespace {

std::string streamSuffix(const std::string& trackSuffix) {
    return navidrome::effectiveStreamSuffix(navidrome::cfg_stream_format.get().c_str(), trackSuffix);
}

void download(const std::string& url, const std::string& partPath, abort_callback& abort,
              threaded_process_status* status) {
    http_request::ptr req = http_client::get()->create_request("GET");
    for (const auto& h : navidrome::parseHeaderLines(navidrome::cfg_custom_headers.get().c_str()))
        req->add_header(h.c_str());
    file::ptr in = req->run(url.c_str(), abort);
    const t_filesize total = in->get_size(abort);

    std::ofstream out(navidrome::offline::fsPath(partPath), std::ios::binary | std::ios::trunc);
    if (!out) throw exception_io_denied();
    std::vector<char> buf(256 * 1024);
    t_filesize done = 0;
    for (;;) {
        const t_size n = in->read(buf.data(), buf.size(), abort);
        if (n == 0) break;
        out.write(buf.data(), static_cast<std::streamsize>(n));
        if (!out) throw exception_io_device_full();
        done += n;
        if (status && total != filesize_invalid && total > 0)
            status->set_progress_secondary_float(static_cast<double>(done) / static_cast<double>(total));
    }
    out.close();
    if (!out) throw exception_io_device_full();
}

bool fetchInto(navidrome::offline::Slot slot, const navidrome::offline::Item& item, const std::string& url,
               abort_callback& abort, threaded_process_status* status, std::string& error) {
    const auto& store = navidrome::offlineStore();
    const std::string part = store.partPath(slot, item.id, item.suffix);
    if (part.empty()) { error = "no file name"; return false; }
    try {
        download(url, part, abort, status);
    } catch (const exception_aborted&) {
        store.discardPart(slot, item.id, item.suffix);
        throw;
    } catch (const std::exception& e) {
        store.discardPart(slot, item.id, item.suffix);
        error = e.what();
        return false;
    }
    return store.commit(slot, item.id, item.suffix, error);
}

std::mutex                            g_prefetchMutex;
std::shared_ptr<abort_callback_impl>  g_prefetchAbort;

void prefetch(navidrome::offline::Item item) {
    auto abort = std::make_shared<abort_callback_impl>();
    {
        std::lock_guard<std::mutex> lock(g_prefetchMutex);
        if (g_prefetchAbort) g_prefetchAbort->abort();
        g_prefetchAbort = abort;
    }
    std::thread([item = std::move(item), abort]() {
        navidrome::dbg::runGuarded("Offline", "prefetch", [&] {
            const std::string url = navidrome::libraryCore().streamURL(item.id);
            if (url.empty()) return;
            NAVIDROME_LOG("Offline", "prefetch id=" + item.id + " suffix=" + item.suffix);
            std::string err;
            bool ok = false;
            try {
                ok = fetchInto(navidrome::offline::Slot::Prefetch, item, url, *abort, nullptr, err);
            } catch (const exception_aborted&) {
                NAVIDROME_LOG("Offline", "prefetch id=" + item.id + " aborted");
                return;
            }
            if (!ok) {
                NAVIDROME_WARN("Offline", "prefetch id=" + item.id + " failed: " + err);
                return;
            }
            const std::size_t evicted =
                navidrome::offlineStore().evictPrefetch(navidrome::prefetchLimitMB() << 20);
            NAVIDROME_LOG("Offline", "prefetch id=" + item.id + " done, evicted " + std::to_string(evicted));
        });
    }).detach();
}

bool nextTrack(const metadb_handle_ptr& playing, metadb_handle_ptr& out, std::string& why) {
    auto pm = playlist_manager::get();
    pfc::list_t<t_playback_queue_item> queue;
    pm->queue_get_contents(queue);
    if (queue.get_count() > 0) { out = queue[0].m_handle; why = "playback queue"; return out.is_valid(); }

    t_size pl = pfc_infinite, idx = pfc_infinite;
    if (!pm->get_playing_item_location(&pl, &idx)) {
        pl = pm->get_playing_playlist();
        if (pl == pfc_infinite || playing.is_empty() || !pm->playlist_find_item(pl, playing, idx)) {
            why = "playing item not in a playlist";
            return false;
        }
    }
    const t_size count = pm->playlist_get_item_count(pl);
    const char* order = pm->playback_order_get_name(pm->playback_order_get_active());
    const std::string orderName = order ? order : "?";
    const bool repeatAll = orderName == "Repeat (playlist)";
    if (!repeatAll && orderName != "Default") { why = "playback order \"" + orderName + "\" can't be predicted"; return false; }
    t_size next = idx + 1;
    if (next >= count) {
        if (!repeatAll) { why = "last track of the playlist"; return false; }
        next = 0;
    }
    if (next == idx) { why = "single-track playlist"; return false; }
    why = "index " + std::to_string(next) + " of " + std::to_string(count) + " (" + orderName + ")";
    return pm->playlist_get_item_handle(out, pl, next);
}

static std::atomic<bool> g_removalPending{ false };

static void purgeDeferredRemovals(const char* when) {
    if (!g_removalPending.exchange(false)) return;
    std::size_t pending = 0;
    const std::size_t purged = navidrome::offlineStore().purgeRemoved(&pending);
    if (purged > 0) NAVIDROME_LOG("Offline", "purged " + std::to_string(purged) + " removed offline cop(ies) " + when);
    if (pending > 0) g_removalPending = true;
}

class navidrome_prefetch_callback : public play_callback_static {
public:
    unsigned get_flags() override { return flag_on_playback_new_track | flag_on_playback_stop; }

    void on_playback_new_track(metadb_handle_ptr playing) override {
        purgeDeferredRemovals("on track change");
        if (!navidrome::prefetchEnabled() || !navidrome::libraryCore().isConfigured()) return;
        metadb_handle_ptr next;
        std::string why;
        if (!nextTrack(playing, next, why)) { NAVIDROME_LOG("Offline", "prefetch: nothing to do, " + why); return; }
        const navidrome::TrackURI t = navidrome::parseTrackURI(next->get_path());
        if (t.id.empty() || t.suffix.empty()) { NAVIDROME_LOG("Offline", "prefetch: next track isn't from Navidrome"); return; }
        const std::string suffix = streamSuffix(t.suffix);
        const auto& store = navidrome::offlineStore();
        if (store.has(navidrome::offline::Slot::Pinned, t.id, t.suffix)
            || store.has(navidrome::offline::Slot::Prefetch, t.id, suffix)) {
            NAVIDROME_LOG("Offline", "prefetch: id=" + t.id + " already cached");
            return;
        }
        NAVIDROME_LOG("Offline", "prefetch: next is " + why);
        prefetch({ t.id, suffix, t.title });
    }

    void on_playback_starting(play_control::t_track_command, bool) override {}
    void on_playback_stop(play_control::t_stop_reason) override { purgeDeferredRemovals("on stop"); }
    void on_playback_seek(double) override {}
    void on_playback_pause(bool) override {}
    void on_playback_edited(metadb_handle_ptr) override {}
    void on_playback_dynamic_info(const file_info&) override {}
    void on_playback_dynamic_info_track(const file_info&) override {}
    void on_playback_time(double) override {}
    void on_volume_change(float) override {}
};

static play_callback_static_factory_t<navidrome_prefetch_callback> g_prefetch_callback;

class navidrome_offline_initquit : public initquit {
public:
    void on_init() override {
        const bool enabled = navidrome::prefetchEnabled();
        const std::uint64_t limit = navidrome::prefetchLimitMB() << 20;
        std::thread([enabled, limit]() {
            navidrome::dbg::runGuarded("Offline", "startup cleanup", [&] {
                const auto& store = navidrome::offlineStore();
                std::size_t pending = 0;
                store.purgeRemoved(&pending);
                if (pending > 0) g_removalPending = true;
                const std::size_t removed = enabled ? store.evictPrefetch(limit) : store.clearPrefetch();
                const auto u = store.usage();
                NAVIDROME_LOG("Offline", std::string("startup: prefetch ") + (enabled ? "on" : "off") +
                              ", removed " + std::to_string(removed) + ", pinned " +
                              std::to_string(u.pinnedFiles) + " (" + navidrome::offline::formatBytes(u.pinnedBytes) +
                              "), prefetched " + std::to_string(u.prefetchFiles) + " (" +
                              navidrome::offline::formatBytes(u.prefetchBytes) + ")");
            });
        }).detach();
    }
    void on_quit() override {
        std::lock_guard<std::mutex> lock(g_prefetchMutex);
        if (g_prefetchAbort) g_prefetchAbort->abort();
    }
};

static initquit_factory_t<navidrome_offline_initquit> g_offline_initquit;

std::vector<navidrome::offline::Item> itemsFromHandles(metadb_handle_list_cref data) {
    std::vector<navidrome::offline::Item> items;
    for (t_size i = 0; i < data.get_count(); ++i) {
        const navidrome::TrackURI t = navidrome::parseTrackURI(data[i]->get_path());
        if (t.id.empty() || t.suffix.empty()) continue;
        items.push_back({ t.id, t.suffix, t.title });
    }
    return items;
}

constexpr unsigned kItemMakeOffline   = 0;

class navidrome_offline_context_menu : public contextmenu_item_simple {
public:
    GUID get_parent() override { return navidrome::guids::contextMenuGroup; }
    unsigned get_num_items() override { return 2; }

    void get_item_name(unsigned index, pfc::string_base& out) override {
        out = navidrome::tr(index == kItemMakeOffline ? "Make Available Offline" : "Remove Offline Copy");
    }

    bool get_item_description(unsigned index, pfc::string_base& out) override {
        out = navidrome::tr(index == kItemMakeOffline
            ? "Downloads the original files into the local offline cache; they then play without the server."
            : "Deletes the offline copies of the selected tracks.");
        return true;
    }

    GUID get_item_guid(unsigned index) override {
        GUID g = navidrome::guids::contextMenuOfflineItemsBase;
        g.Data4[7] = static_cast<unsigned char>(0x20 + index);
        return g;
    }

    bool context_get_display(unsigned index, metadb_handle_list_cref data,
                             pfc::string_base& out, unsigned& flags, const GUID& caller) override {
        (void)flags; (void)caller;
        bool anyOurs = false;
        for (t_size i = 0; i < data.get_count() && !anyOurs; ++i)
            anyOurs = !navidrome::trackIdFromURI(data[i]->get_path()).empty();
        if (!anyOurs) return false;
        get_item_name(index, out);
        return true;
    }

    void context_command(unsigned index, metadb_handle_list_cref data, const GUID& caller) override {
        (void)caller;
        auto items = itemsFromHandles(data);
        if (items.empty()) return;
        if (index == kItemMakeOffline) {
            navidrome::makeAvailableOffline(std::move(items));
            return;
        }
        std::vector<std::string> ids;
        for (const auto& it : items) ids.push_back(it.id);
        navidrome::removeOfflineCopies(ids);
    }
};

static contextmenu_item_factory_t<navidrome_offline_context_menu> g_offline_context_menu;

}

std::string navidrome::offlineCachedPath(const TrackURI& track) {
    if (track.id.empty()) return {};
    std::vector<std::string> suffixes;
    if (!track.suffix.empty()) suffixes.push_back(track.suffix);
    const std::string streamed = streamSuffix(track.suffix);
    if (!streamed.empty() && streamed != track.suffix) suffixes.push_back(streamed);
    return offlineStore().find(track.id, suffixes);
}

void navidrome::makeAvailableOffline(std::vector<offline::Item> items) {
    if (!libraryCore().isConfigured()) {
        reportError(tr("Make Available Offline"),
                    tr("Set up the Navidrome server first: Preferences > Tools > Navidrome."));
        return;
    }
    struct State {
        std::vector<offline::Item> items;
        std::size_t done = 0, skipped = 0, failed = 0;
        std::uint64_t bytes = 0;
        std::string lastError;
    };
    auto state = std::make_shared<State>();
    state->items = std::move(items);
    NAVIDROME_LOG("Offline", "make available offline: " + std::to_string(state->items.size()) + " track(s)");

    auto cb = threaded_process_callback_lambda::create();
    cb->m_run = [state](threaded_process_status& status, abort_callback& abort) {
        const auto& store = offlineStore();
        for (std::size_t i = 0; i < state->items.size(); ++i) {
            abort.check();
            const auto& item = state->items[i];
            status.set_progress(i, state->items.size());
            status.set_item(item.title.empty() ? item.id.c_str() : item.title.c_str());
            if (store.has(offline::Slot::Pinned, item.id, item.suffix)) { ++state->skipped; continue; }
            std::string err;
            if (fetchInto(offline::Slot::Pinned, item, libraryCore().downloadURL(item.id), abort, &status, err)) {
                ++state->done;
            } else {
                ++state->failed;
                state->lastError = err;
                NAVIDROME_WARN("Offline", "download id=" + item.id + " failed: " + err);
            }
        }
        state->bytes = store.usage().pinnedBytes;
    };
    cb->m_on_done = [state](threaded_process_callback::ctx_t, bool aborted) {
        NAVIDROME_LOG("Offline", "make available offline: done=" + std::to_string(state->done) +
                      " skipped=" + std::to_string(state->skipped) + " failed=" + std::to_string(state->failed) +
                      (aborted ? " (aborted)" : ""));
        if (aborted) return;
        std::string msg = trf("{0} track(s) made available offline, {1} already were.",
                              { std::to_string(state->done), std::to_string(state->skipped) });
        msg += " " + trf("Offline cache: {0}.", { offline::formatBytes(state->bytes) });
        if (state->failed > 0) {
            msg += "\n\n" + trf("{0} track(s) failed: {1}", { std::to_string(state->failed), state->lastError });
            reportError(tr("Make Available Offline"), msg);
            return;
        }
        console::printf("Navidrome: %s", msg.c_str());
        showConfirmation(tr("Make Available Offline"), msg);
    };
    threaded_process::g_run_modeless(cb,
        threaded_process::flag_show_abort | threaded_process::flag_show_item |
        threaded_process::flag_show_progress | threaded_process::flag_show_progress_dual |
        threaded_process::flag_show_delayed,
        core_api::get_main_window(), tr("Make Available Offline"));
}

void navidrome::removeOfflineCopies(const std::vector<std::string>& ids) {
    std::size_t removed = 0, deferred = 0;
    for (const auto& id : ids) removed += offlineStore().removePinned(id, &deferred);
    if (deferred > 0) g_removalPending = true;
    NAVIDROME_LOG("Offline", "removed " + std::to_string(removed) + " offline cop(ies) for " +
                  std::to_string(ids.size()) + " id(s)" +
                  (deferred > 0 ? ", " + std::to_string(deferred) + " in use (deleted once released)" : ""));
    console::print(trf("Navidrome: removed {0} offline copy/copies.", { std::to_string(removed) }).c_str());
}

namespace {

std::vector<navidrome::offline::Item> itemsFromNodes(const std::vector<navidrome::BrowserNodePtr>& songs) {
    std::vector<navidrome::offline::Item> items;
    for (const auto& n : songs) {
        if (!n || n->type != navidrome::BrowserNode::Song || n->id.empty() || n->suffix.empty()) continue;
        items.push_back({ n->id, n->suffix, n->displayName });
    }
    return items;
}

}

void navidrome::makeNodesAvailableOffline(const std::vector<BrowserNodePtr>& songs) {
    auto items = itemsFromNodes(songs);
    if (items.empty()) return;
    makeAvailableOffline(std::move(items));
}

void navidrome::removeNodesOfflineCopies(const std::vector<BrowserNodePtr>& songs) {
    std::vector<std::string> ids;
    for (const auto& it : itemsFromNodes(songs)) ids.push_back(it.id);
    if (!ids.empty()) removeOfflineCopies(ids);
}
