#include "stdafx.h"
#include "NavidromeSdkGlue.h"
#include "NavidromePlaylistSync.h"
#include "NavidromeBrowserEnqueue.h"
#include "NavidromeRatingService.h"
#include "NavidromeLibraryService.h"
#include "NavidromeLyricsService.h"
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
#include <functional>
#include <chrono>
#include <unordered_map>
#include <unordered_set>
#include <cstring>
#include <cstdlib>
#include <mutex>
#include <thread>

namespace {

class navidrome_rating_api_impl : public navidrome::navidrome_rating_api {
public:
    bool is_navidrome_track(const metadb_handle_ptr& track) override {
        return track.is_valid() && !navidrome::trackIdFromURI(track->get_path()).empty();
    }

    void set_rating_async(const metadb_handle_ptr& track, int stars) override {
        if (track.is_empty()) return;
        std::string songId = navidrome::trackIdFromURI(track->get_path());
        if (songId.empty()) return;
        if (stars < 0) stars = 0;
        if (stars > 5) stars = 5;

        navidrome::RatingUpdate u;
        u.songId = songId;
        u.rating = stars;
        file_info_impl info;
        if (track->get_info(info))
            u.starred = info.meta_get_count_by_name(navidrome::kStarredTag) > 0;

        std::thread([u = std::move(u)]() mutable {
            if (navidrome::setRatingOnServer(u.songId, u.rating))
                navidrome::syncRatingsToPlaylists({ std::move(u) });
        }).detach();
    }
};

static service_factory_single_t<navidrome_rating_api_impl> g_navidrome_rating_api_factory;

class navidrome_library_api_impl : public navidrome::navidrome_library_api_v2 {
public:
    bool is_configured() override { return navidrome::libraryCore().isConfigured(); }

    bool list_albums(navidrome::library_album_sink& sink, abort_callback& abort,
                     pfc::string_base& errorOut) override {
        return walk_albums(abort, errorOut, [&](const navidrome::Album& al) {
            sink.on_album(al.id.c_str(), al.name.c_str(), al.artist.c_str(), al.artistId.c_str(),
                          al.coverArtId.c_str(), al.year, al.songCount);
        });
    }

    bool list_albums_v2(navidrome::library_album_sink_v2& sink, abort_callback& abort,
                        pfc::string_base& errorOut) override {
        return walk_albums(abort, errorOut, [&](const navidrome::Album& al) {
            sink.on_album(al.id.c_str(), al.name.c_str(), al.artist.c_str(), al.artistId.c_str(),
                          al.coverArtId.c_str(), al.year, al.songCount, al.created.c_str());
        });
    }

    album_art_data_ptr fetch_cover(const char* coverArtId, int size, abort_callback& abort) override {
        const std::string id = coverArtId ? coverArtId : "";
        if (id.empty()) throw exception_album_art_not_found();
        const std::vector<uint8_t> bytes = navidrome::libraryFetchCover(id, size, abort);
        abort.check();
        if (bytes.empty()) throw exception_album_art_not_found();
        return album_art_data_impl::g_create(bytes.data(), bytes.size());
    }

    void play_album(const char* albumId, bool replace, bool play) override {
        const std::string id = albumId ? albumId : "";
        if (id.empty()) return;
        std::thread([id, replace, play]() {
            std::string err;
            std::vector<navidrome::BrowserNodePtr> nodes;
            for (const auto& s : navidrome::libraryClient().getSongsForAlbum(id, err))
                nodes.push_back(navidrome::makeSongNode(s));
            enqueue(std::move(nodes), replace, play, "album " + id, err);
        }).detach();
    }

    void play_artist(const char* artistId, bool replace, bool play) override {
        const std::string id = artistId ? artistId : "";
        if (id.empty()) return;
        std::thread([id, replace, play]() {
            std::string err;
            auto nodes = navidrome::collectArtistSongs(navidrome::libraryClient(), id, err);
            enqueue(std::move(nodes), replace, play, "artist " + id, err);
        }).detach();
    }

private:
    static bool walk_albums(abort_callback& abort, pfc::string_base& errorOut,
                            const std::function<void(const navidrome::Album&)>& emit) {
        std::string err;
        size_t n = 0;
        const bool ok = navidrome::listLibraryAlbums(
            navidrome::libraryClient(), [&abort] { return abort.is_aborting(); },
            [&](const navidrome::Album& al) { emit(al); ++n; }, err);
        if (!ok) {
            NAVIDROME_WARN("Library", "list_albums failed after " + std::to_string(n) + " albums: " + err);
            errorOut = err.c_str();
            return false;
        }
        NAVIDROME_LOG("Library", "list_albums: " + std::to_string(n) + " albums");
        return true;
    }

    static void enqueue(std::vector<navidrome::BrowserNodePtr> nodes, bool replace, bool play,
                        const std::string& what, const std::string& err) {
        if (nodes.empty()) {
            NAVIDROME_WARN("Library", "play " + what + ": no tracks" + (err.empty() ? "" : " (" + err + ")"));
            return;
        }
        fb2k::inMainThread([nodes = std::move(nodes), replace, play]() {
            std::string status;
            navidrome::enqueueBrowserNodes(nodes, play, replace,
                                           [](const std::string&) { return std::string(); }, status);
        });
    }
};

static service_factory_single_t<navidrome_library_api_impl> g_navidrome_library_api_factory;

class navidrome_lyrics_api_impl : public navidrome::navidrome_lyrics_api {
public:
    bool is_navidrome_track(const metadb_handle_ptr& track) override {
        return track.is_valid() && !navidrome::trackIdFromURI(track->get_path()).empty();
    }

    bool get_lyrics(const metadb_handle_ptr& track, navidrome::lyrics_sink& sink,
                    abort_callback& abort, pfc::string_base& errorOut) override {
        if (!is_navidrome_track(track) || !navidrome::libraryCore().isConfigured()) return false;
        abort.check();
        std::string err;
        const navidrome::Lyrics l =
            navidrome::lyricsForTrackURI(navidrome::libraryClient(), track->get_path(), err);
        abort.check();
        if (!err.empty()) {
            NAVIDROME_WARN("Lyrics", "get_lyrics failed: " + err);
            errorOut = err.c_str();
            return false;
        }
        if (l.empty()) return false;
        sink.on_begin(l.synced, static_cast<unsigned>(l.lines.size()));
        for (const auto& line : l.lines)
            sink.on_line(static_cast<int>(line.startMs), line.text.c_str());
        return true;
    }
};

static service_factory_single_t<navidrome_lyrics_api_impl> g_navidrome_lyrics_api_factory;
}
