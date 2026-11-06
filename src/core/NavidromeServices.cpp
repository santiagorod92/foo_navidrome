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
#include "NavidromeLastFm.h"
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

class navidrome_library_api_impl : public navidrome::navidrome_library_api_v4 {
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

    static bool track_handle(const navidrome::Song& s, const metadb_hint_list::ptr& hints, metadb_handle_ptr& handle) {
        const navidrome::TrackURI t = navidrome::trackURIFromNode(*navidrome::makeSongNode(s));
        const std::string uri = navidrome::buildTrackURI(t);
        if (uri.empty()) return false;
        playable_location_impl loc;
        loc.set_path(uri.c_str());
        loc.set_subsong(0);
        metadb::get()->handle_create(handle, loc);
        file_info_impl info;
        navidrome::applyTrackInfo(t, info);
        navidrome::applyPlayStats(info, s.plays);
        hints->add_hint(handle, info, filestats_invalid, true);
        return true;
    }

    bool list_tracks(navidrome::library_track_sink& sink, abort_callback& abort,
                     pfc::string_base& errorOut) override {
        if (!is_configured()) {
            errorOut = "foo_navidrome has no server configured";
            return false;
        }
        std::string err;
        const auto songs = navidrome::libraryClient().getAllSongs(err);
        abort.check();
        if (!err.empty()) {
            errorOut = err.c_str();
            return false;
        }
        auto hints = metadb_io_v2::get()->create_hint_list();
        std::size_t count = 0;
        for (const auto& s : songs) {
            abort.check();
            metadb_handle_ptr handle;
            if (!track_handle(s, hints, handle)) continue;
            sink.on_track(handle);
            ++count;
        }
        fb2k::inMainThread([hints]() { hints->on_done(); });
        NAVIDROME_LOG("Library", "list_tracks: " + std::to_string(count) + " tracks");
        return true;
    }

    bool artist_info(const char* artistName, navidrome::artist_info_sink& sink, abort_callback& abort,
                     pfc::string_base& errorOut) override {
        if (!is_configured()) {
            errorOut = "foo_navidrome has no server configured";
            return false;
        }
        const std::string name = artistName ? artistName : "";
        const auto lower = [](std::string s) {
            for (auto& c : s) c = (char)std::tolower((unsigned char)c);
            return s;
        };
        std::string err;
        const auto artists = navidrome::libraryClient().getArtists(err);
        abort.check();
        if (!err.empty()) {
            errorOut = err.c_str();
            return false;
        }
        const std::string want = lower(name);
        const auto it = std::find_if(artists.begin(), artists.end(),
                                     [&](const navidrome::Artist& a) { return lower(a.name) == want; });
        if (it == artists.end()) {
            errorOut = ("no artist named \"" + name + "\" on the server").c_str();
            return false;
        }
        const navidrome::ArtistInfo info = navidrome::artistInfoForDisplay(it->id, it->name, err);
        abort.check();
        if (!err.empty()) {
            errorOut = err.c_str();
            return false;
        }
        const std::string& image = !info.largeImageUrl.empty() ? info.largeImageUrl
                                 : !info.mediumImageUrl.empty() ? info.mediumImageUrl : info.smallImageUrl;
        sink.on_artist_info(it->id.c_str(), info.biography.c_str(), image.c_str(), info.lastFmUrl.c_str());
        NAVIDROME_LOG("Library", "artist_info: \"" + it->name + "\"" + (image.empty() ? " (no image)" : ""));
        return true;
    }

    bool search(const char* query, navidrome::library_search_sink& sink, abort_callback& abort,
                pfc::string_base& errorOut) override {
        if (!is_configured()) {
            errorOut = "foo_navidrome has no server configured";
            return false;
        }
        const std::string q = query ? query : "";
        std::string err;
        const navidrome::SearchResults found = navidrome::libraryClient().search(q, err);
        abort.check();
        if (!err.empty()) {
            errorOut = err.c_str();
            return false;
        }
        for (const auto& a : found.artists) sink.on_artist(a.id.c_str(), a.name.c_str(), a.coverArtId.c_str(), a.albumCount);
        for (const auto& al : found.albums)
            sink.on_album(al.id.c_str(), al.name.c_str(), al.artist.c_str(), al.artistId.c_str(), al.coverArtId.c_str(), al.year,
                          al.songCount, al.created.c_str());
        auto hints = metadb_io_v2::get()->create_hint_list();
        for (const auto& s : found.songs) {
            abort.check();
            metadb_handle_ptr handle;
            if (track_handle(s, hints, handle)) sink.on_track(handle);
        }
        fb2k::inMainThread([hints]() { hints->on_done(); });
        NAVIDROME_LOG("Library", "search: " + std::to_string(found.artists.size()) + " artists, " + std::to_string(found.albums.size()) +
                                     " albums, " + std::to_string(found.songs.size()) + " songs");
        return true;
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
