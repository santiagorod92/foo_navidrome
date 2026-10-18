#pragma once

#include "NavidromeBrowserModel.h"
#include "SubsonicCore.h"

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace navidrome {

class CoreBrowserClient final : public IBrowserClient {
public:
    using HiddenCategoriesFn = std::function<CategoryKindList()>;

    explicit CoreBrowserClient(SubsonicCore& core, HiddenCategoriesFn hidden = {})
        : m_core(core), m_hidden(std::move(hidden)) {}

    std::vector<Artist> getArtists(std::string& e) override { return m_core.getArtists(e); }
    std::vector<Artist> getArtistsForLibrary(const std::string& id, std::string& e) override {
        return m_core.getArtistsForLibrary(id, e);
    }
    std::vector<Album> getAlbumsForArtist(const std::string& id, const std::string& scope,
                                          std::string& e) override {
        return m_core.getAlbumsForArtist(id, e, scope);
    }
    std::vector<Song> getSongsForAlbum(const std::string& id, std::string& e) override {
        return m_core.getSongsForAlbum(id, e);
    }
    std::vector<Song> getPlaylistSongs(const std::string& id, std::string& e) override {
        return m_core.getPlaylistSongs(id, e);
    }
    std::vector<Song> getSongsForGenre(const std::string& g, int n, std::string& e) override {
        return m_core.getSongsForGenre(g, n, e);
    }
    std::vector<Song> getStarredSongs(std::string& e) override { return m_core.getStarredSongs(e); }
    std::vector<Genre> getGenres(std::string& e) override { return m_core.getGenres(e); }
    std::vector<Playlist> getPlaylists(std::string& e) override { return m_core.getPlaylists(e); }
    std::vector<Album> getAlbumList(AlbumListType t, int n, std::string& e) override {
        return m_core.getAlbumList(t, n, e);
    }
    std::vector<Album> getAlbumsByYear(int from, int to, int n, std::string& e) override {
        return m_core.getAlbumsByYear(from, to, n, e);
    }
    std::vector<RadioStation> getRadioStations(std::string& e) override {
        return m_core.getRadioStations(e);
    }
    std::vector<Bookmark> getBookmarks(std::string& e) override { return m_core.getBookmarks(e); }
    std::vector<PodcastChannel> getPodcastChannels(std::string& e) override {
        return m_core.getPodcastChannels(e);
    }
    std::vector<PodcastEpisode> getPodcastEpisodes(const std::string& id, std::string& e) override {
        return m_core.getPodcastEpisodes(id, e);
    }
    std::vector<NowPlayingEntry> getNowPlaying(std::string& e) override {
        return m_core.getNowPlaying(e);
    }
    std::vector<Song> getSimilarSongs(const std::string& id, int n, std::string& e) override {
        return m_core.getSimilarSongs(id, n, e);
    }
    std::vector<Song> getRandomSongs(int n, std::string& e) override {
        return m_core.getRandomSongs(n, e);
    }
    std::vector<Song> getAllSongs(std::string& e) override { return m_core.getAllSongs(e); }
    ArtistInfo getArtistInfo(const std::string& id, std::string& e) override {
        return m_core.getArtistInfo(id, e);
    }
    std::vector<Song> getTopSongs(const std::string& name, int n, std::string& e) override {
        return m_core.getTopSongs(name, n, e);
    }
    Lyrics getLyrics(const std::string& id, const std::string& artist, const std::string& title,
                     std::string& e) override {
        return m_core.getLyrics(id, artist, title, e);
    }
    SearchResults search(const std::string& query, std::string& e) override {
        return m_core.search(query, e);
    }

    std::vector<std::string> groupingLibraryIds() override { return m_core.libraryGroupingIds(); }
    CategoryKindList hiddenCategories() override { return m_hidden ? m_hidden() : CategoryKindList{}; }
    std::vector<MusicFolder> musicFolders() override { return m_core.cachedMusicFolders(); }

    bool setStarred(bool starred, const std::string& id, StarKind kind, std::string& e) override {
        return withFallback(m_core.setStarred(starred, id, kind, e), e, "request failed");
    }
    bool setRating(int stars, const std::string& id, std::string& e) override {
        return withFallback(m_core.setRating(stars, id, e), e, "request failed");
    }
    bool getSong(const std::string& id, Song& out, std::string& e) override {
        return withFallback(!id.empty() && m_core.getSong(id, out, e), e, "song not found");
    }
    bool getPlayQueue(PlayQueue& out, std::string& e) override { return m_core.getPlayQueue(out, e); }
    bool savePlayQueue(const std::vector<std::string>& ids, const std::string& current,
                       double positionMs, std::string& e) override {
        return m_core.savePlayQueue(ids, current, positionMs, e);
    }

private:
    static bool withFallback(bool ok, std::string& e, const char* fallback) {
        if (!ok && e.empty()) e = fallback;
        return ok;
    }

    SubsonicCore&      m_core;
    HiddenCategoriesFn m_hidden;
};

}
