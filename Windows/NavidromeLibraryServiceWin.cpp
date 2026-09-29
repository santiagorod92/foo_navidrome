// Windows implementation of navidrome_library_api (NavidromeLibraryService.h): publishes the
// server's album list, cover art and a play-album action to other components.
#include "stdafx.h"
#include "SubsonicClientWin.h"
#include "MediaEnrichmentLogic.h"
#include "../NavidromeLibraryService.h"
#include "../NavidromeBrowserEnqueue.h"
#include "../NavidromeBrowserModel.h"
#include <SDK/album_art_helpers.h>
#include <thread>

namespace {

class navidrome_library_api_impl : public navidrome::navidrome_library_api {
public:
    bool is_configured() override {
        return navidrome::SubsonicClientWin::get().isConfigured();
    }

    bool list_albums(navidrome::library_album_sink& sink, abort_callback& abort,
                     pfc::string_base& errorOut) override {
        auto& client = navidrome::SubsonicClientWin::get();
        std::string err;
        auto artists = client.getArtists(err);
        if (artists.empty() && !err.empty()) { errorOut = err.c_str(); return false; }
        for (const auto& artist : artists) {
            if (abort.is_aborting()) return false;
            std::string aerr;
            auto albums = client.getAlbumsForArtist(artist.id, aerr);
            for (const auto& al : albums) {
                sink.on_album(al.id.c_str(), al.name.c_str(),
                              (al.artist.empty() ? artist.name : al.artist).c_str(),
                              (al.artistId.empty() ? artist.id : al.artistId).c_str(),
                              (al.coverArtId.empty() ? al.id : al.coverArtId).c_str(),
                              al.year, al.songCount);
            }
        }
        return true;
    }

    album_art_data_ptr fetch_cover(const char* coverArtId, int size, abort_callback& abort) override {
        const std::string id = coverArtId ? coverArtId : "";
        if (id.empty()) throw exception_album_art_not_found();
        auto& client = navidrome::SubsonicClientWin::get();
        auto ctx = client.snapshot();
        // Sized thumbnails are cached under their own key so they never shadow (or get shadowed
        // by) the original-size entry the album-art extractor stores under the bare id.
        const std::string key = size > 0 ? id + "@" + std::to_string(size) : id;
        auto cached = navidrome::CoverCache::instance().get(ctx.serverUrl, ctx.username, key);
        if (!cached.empty()) return album_art_data_impl::g_create(cached.data(), cached.size());

        auto result = client.httpGetBinary(ctx, client.coverArtURL(ctx, id, size),
                                           20u * 1024 * 1024, abort);
        if (result.cls == navidrome::FetchClass::Aborted) throw exception_aborted();
        if (result.cls != navidrome::FetchClass::Ok) throw exception_album_art_not_found();
        navidrome::CoverCache::instance().put(ctx.serverUrl, ctx.username, key, result.body);
        return album_art_data_impl::g_create(result.body.data(), result.body.size());
    }

    void play_album(const char* albumId, bool replace, bool play) override {
        const std::string id = albumId ? albumId : "";
        if (id.empty()) return;
        std::thread([id, replace, play]() {
            std::string err;
            auto songs = navidrome::SubsonicClientWin::get().getSongsForAlbum(id, err);
            if (songs.empty()) return;
            std::vector<navidrome::BrowserNodePtr> nodes;
            for (const auto& s : songs) nodes.push_back(navidrome::makeSongNode(s));
            fb2k::inMainThread([nodes = std::move(nodes), replace, play]() {
                std::string status;
                navidrome::enqueueBrowserNodes(nodes, play, replace,
                                               [](const std::string&) { return std::string(); }, status);
            });
        }).detach();
    }

    void play_artist(const char* artistId, bool replace, bool play) override {
        const std::string id = artistId ? artistId : "";
        if (id.empty()) return;
        std::thread([id, replace, play]() {
            auto& client = navidrome::SubsonicClientWin::get();
            std::string err;
            std::vector<navidrome::BrowserNodePtr> nodes;
            for (const auto& al : client.getAlbumsForArtist(id, err)) {
                std::string serr;
                for (const auto& s : client.getSongsForAlbum(al.id, serr))
                    nodes.push_back(navidrome::makeSongNode(s));
            }
            if (nodes.empty()) return;
            fb2k::inMainThread([nodes = std::move(nodes), replace, play]() {
                std::string status;
                navidrome::enqueueBrowserNodes(nodes, play, replace,
                                               [](const std::string&) { return std::string(); }, status);
            });
        }).detach();
    }
};

static service_factory_single_t<navidrome_library_api_impl> g_navidrome_library_api_factory;

} // namespace
