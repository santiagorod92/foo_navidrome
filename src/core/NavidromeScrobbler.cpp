#include "stdafx.h"
#include "NavidromeLibraryPlatform.h"
#include "NavidromePlaylistSync.h"
#include "NavidromeSettings.h"
#include "NavidromeDebugLog.h"
#include "ScrobbleQueue.h"
#include "SubsonicTypes.h"
#include <SDK/initquit.h>
#include <SDK/play_callback.h>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

navidrome::ScrobbleQueue& scrobbleQueue() {
    static navidrome::ScrobbleQueue queue(
        filesystem::g_get_native_path(core_api::pathInProfile("foo_navidrome-scrobbles.tsv")).c_str());
    return queue;
}

std::mutex g_flushMutex;

bool flushScrobbleQueue(const char* trigger) {
    std::unique_lock<std::mutex> lock(g_flushMutex, std::try_to_lock);
    if (!lock.owns_lock()) return false;
    auto& queue = scrobbleQueue();
    if (queue.size() == 0) return true;
    auto& core = navidrome::libraryCore();
    const navidrome::ScrobbleFlushResult r = navidrome::flushPendingScrobbles(
        queue, [&](const std::vector<navidrome::PendingScrobble>& batch) {
            std::string err;
            core.submitScrobbles(batch, err);
            return core.lastError();
        });
    if (r.sent || r.dropped)
        NAVIDROME_LOG("Scrobble", std::string("queue flush (") + trigger + "): sent " + std::to_string(r.sent) +
                      ", dropped " + std::to_string(r.dropped) + ", " + std::to_string(r.remaining) + " left");
    if (!r.stoppedBy.ok())
        NAVIDROME_WARN("Scrobble", std::string("queue flush (") + trigger + ") stopped, " +
                       std::to_string(r.remaining) + " pending: " + r.stoppedBy.kindName() + ": " +
                       r.stoppedBy.message);
    if (r.sent)
        console::print(navidrome::trf("Navidrome: submitted {0} scrobble(s) saved while the server couldn't be reached",
                                      { std::to_string(r.sent) }).c_str());
    return r.remaining == 0;
}

void queueScrobble(const std::string& songId, std::int64_t timeMs, const std::string& why) {
    const std::size_t pending = scrobbleQueue().add({ songId, timeMs });
    NAVIDROME_WARN("Scrobble", "submit id=" + songId + " queued (" + why + "), " +
                   std::to_string(pending) + " pending");
}

std::int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

void scrobbleAsync(std::string songId, bool submission) {
    const std::int64_t playedAtMs = nowMs();
    std::thread([songId, submission, playedAtMs]() {
        navidrome::dbg::runGuarded("Scrobble", "scrobbleAsync", [&] {
            auto& core = navidrome::libraryCore();
            if (submission && !flushScrobbleQueue("submit") && scrobbleQueue().size() > 0) {
                queueScrobble(songId, playedAtMs, "older scrobbles still pending");
                return;
            }
            std::string err;
            core.scrobble(songId, submission, err);
            const navidrome::Error e = core.lastError();
            NAVIDROME_LOG("Scrobble", std::string(submission ? "submit" : "now-playing") +
                          " id=" + songId + (e.ok() ? " ok"
                          : std::string(" FAILED ") + e.kindName() + ": " + e.message));
            if (submission && navidrome::shouldQueueScrobble(e))
                queueScrobble(songId, playedAtMs, e.kindName());
            else if (!submission && e.ok())
                flushScrobbleQueue("now-playing");
        });
    }).detach();
}

void refreshRatingAsync(std::string songId) {
    std::thread([songId]() {
        navidrome::dbg::runGuarded("Rating", "refreshRatingAsync", [&] {
            auto& core = navidrome::libraryCore();
            std::string err;
            navidrome::Song song;
            if (!core.getSong(songId, song, err)) {
                NAVIDROME_WARN("Rating", "getSong id=" + songId + " failed: " + core.lastError().kindName());
                return;
            }
            navidrome::RatingUpdate u;
            u.songId  = songId;
            u.rating  = song.rating;
            u.starred = song.starred;
            NAVIDROME_LOG("Rating", "id=" + songId + " -> rating=" + std::to_string(u.rating) +
                          " starred=" + (u.starred ? "1" : "0"));
            std::vector<navidrome::RatingUpdate> updates;
            updates.push_back(std::move(u));
            navidrome::syncRatingsToPlaylists(std::move(updates));
        });
    }).detach();
}

class navidrome_scrobbler : public play_callback_static {
public:
    unsigned get_flags() override {
        return flag_on_playback_new_track | flag_on_playback_time | flag_on_playback_stop;
    }

    void on_playback_new_track(metadb_handle_ptr track) override {
        auto a = m_tracker.onNewTrack(
            track.is_empty() ? std::string() : std::string(track->get_path()),
            track.is_empty() ? 0.0 : track->get_length(),
            navidrome::cfg_scrobble.get());
        if (!a.refreshRatingId.empty()) refreshRatingAsync(a.refreshRatingId);
        if (!a.scrobbleNowId.empty())   scrobbleAsync(a.scrobbleNowId, false);
    }

    void on_playback_time(double time) override {
        std::string id = m_tracker.onPlaybackTime(time);
        if (!id.empty()) scrobbleAsync(id, true);
    }

    void on_playback_stop(play_control::t_stop_reason) override { m_tracker.onStop(); }

    void on_playback_starting(play_control::t_track_command, bool) override {}
    void on_playback_seek(double) override {}
    void on_playback_pause(bool) override {}
    void on_playback_edited(metadb_handle_ptr) override {}
    void on_playback_dynamic_info(const file_info&) override {}
    void on_playback_dynamic_info_track(const file_info&) override {}
    void on_volume_change(float) override {}

private:
    navidrome::ScrobbleTracker m_tracker;
};

play_callback_static_factory_t<navidrome_scrobbler> g_navidrome_scrobbler_factory;

void logSessionEnv() {
#ifdef NAVIDROME_DEBUG_LOG
    navidrome::SessionEnv e;
#ifdef __APPLE__
    e.platform        = "macOS";
#else
    e.platform        = "Windows";
#endif
    e.configured      = navidrome::libraryCore().isConfigured();
    e.serverUrl       = navidrome::cfg_server_url.get().c_str();
    e.transcodeFormat = navidrome::cfg_stream_format.get().c_str();
    e.maxBitrate      = (int)navidrome::cfg_max_bitrate.get();
    e.scrobble        = navidrome::cfg_scrobble.get();
    e.startupRefresh  = navidrome::refreshRatingsOnStartEnabled();
    e.customHeaders   = navidrome::cfg_custom_headers.get().length() > 0;
    NAVIDROME_LOG("Env", navidrome::describeSessionEnv(e));
#endif
}

void refreshRatingsOnStart() {
    logSessionEnv();

    navidrome::PlaylistAlbumScan scan = navidrome::scanPlaylistAlbums();
    if (scan.entries == 0) return;

    if (!navidrome::refreshRatingsOnStartEnabled()) {
        NAVIDROME_LOG("Rating", "startup refresh: disabled by advconfig switch");
        return;
    }
    if (!navidrome::libraryCore().isConfigured()) {
        NAVIDROME_LOG("Rating", "startup refresh: no server configured");
        return;
    }
    NAVIDROME_LOG("Rating", "startup refresh: " + std::to_string(scan.entries) +
                  " entries, " + std::to_string(scan.albumIds.size()) + " distinct albums, " +
                  std::to_string(scan.ungrouped) + " ungrouped");

    if (scan.albumIds.empty()) {
        console::print(navidrome::ratingRefreshNoAlbumIdsMessage(scan.entries).c_str());
        return;
    }

    const std::size_t ungrouped = scan.ungrouped;
    std::thread([albumIds = std::move(scan.albumIds), ungrouped]() {
        navidrome::dbg::runGuarded("Rating", "startup refresh worker", [&] {
            auto& core = navidrome::libraryCore();
            std::vector<navidrome::RatingUpdate> updates;
            std::size_t failed = 0;
            for (const auto& albumId : albumIds) {
                std::string err;
                auto songs = core.getSongsForAlbum(albumId, err);
                if (!err.empty()) { ++failed; continue; }
                for (const auto& song : songs) {
                    if (song.id.empty()) continue;
                    navidrome::RatingUpdate u;
                    u.songId  = song.id;
                    u.rating  = song.rating;
                    u.starred = song.starred;
                    updates.push_back(std::move(u));
                }
            }
            navidrome::syncRatingsToPlaylists(std::move(updates));
            const std::string msg = navidrome::ratingRefreshSummary(albumIds.size() - failed, failed, ungrouped);
            console::print(msg.c_str());
            NAVIDROME_LOG("Rating", "startup refresh done: " + std::to_string(albumIds.size() - failed) +
                          " album(s), " + std::to_string(failed) + " failed, " + std::to_string(ungrouped) +
                          " entr(ies) without album id");
        });
    }).detach();
}

void flushScrobblesOnStart() {
    const std::size_t pending = scrobbleQueue().size();
    if (pending == 0) return;
    if (!navidrome::cfg_scrobble.get() || !navidrome::libraryCore().isConfigured()) {
        NAVIDROME_LOG("Scrobble", std::to_string(pending) + " queued scrobble(s) kept: scrobbling off or no server");
        return;
    }
    NAVIDROME_LOG("Scrobble", std::to_string(pending) + " queued scrobble(s) at startup");
    std::thread([]() {
        navidrome::dbg::runGuarded("Scrobble", "startup flush", [] { flushScrobbleQueue("startup"); });
    }).detach();
}

class navidrome_startup_refresh : public initquit {
public:
    void on_init() override {
        refreshRatingsOnStart();
        flushScrobblesOnStart();
    }
};

initquit_factory_t<navidrome_startup_refresh> g_navidrome_startup_refresh_factory;

}

bool navidrome::setRatingOnServer(const std::string& songId, int rating) {
    std::string err;
    return navidrome::libraryCore().setRating(rating, songId, err);
}

bool navidrome::setStarredOnServer(const std::string& songId, bool starred) {
    std::string err;
    return navidrome::libraryCore().setStarred(starred, songId, navidrome::StarKind::Song, err);
}
