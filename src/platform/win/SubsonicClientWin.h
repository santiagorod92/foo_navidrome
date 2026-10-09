#pragma once
#include "../../core/SubsonicTypes.h"
#include "../../core/SubsonicCore.h"
#include "../../core/MediaEnrichmentLogic.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace navidrome {

struct SubsonicRequestContext {
    std::string serverUrl;
    std::string username;
    std::string password;
    std::string salt;
    std::string customHeaders;
};

class SubsonicClientWin {
public:
    static SubsonicClientWin& get();

    bool isConfigured() const;
    SubsonicRequestContext snapshot() const;
    bool ping(std::string& outError);
    bool serverInfo(ServerInfo& out, std::string& outError);

    const Error& lastError() const { return m_core->lastError(); }

    std::vector<MusicFolder> getMusicFolders(std::string& outError);
    std::vector<MusicFolder> cachedMusicFolders();
    void refreshMusicFolders();
    std::vector<std::string> libraryGroupingIds();

    std::vector<Artist>  getArtists(std::string& outError);
    std::vector<Artist>  getArtistsForLibrary(const std::string& libraryId,
                                              std::string& outError);
    std::vector<Album>   getAlbumsForArtist(const std::string& artistId,
                                            std::string& outError,
                                            const std::string& scopeLibraryId = "");
    std::vector<Song>    getSongsForAlbum(const std::string& albumId, std::string& outError);
    SearchResults        search(const std::string& query, std::string& outError);

    std::vector<Album>   getAlbumList(AlbumListType type, int size, std::string& outError);
    std::vector<Song>    getStarredSongs(std::string& outError);

    std::vector<Genre>   getGenres(std::string& outError);
    std::vector<Song>    getSongsForGenre(const std::string& genre, int count,
                                          std::string& outError);

    std::vector<Song>    getSimilarSongs(const std::string& itemId, int count,
                                         std::string& outError);

    std::vector<Song>    getRandomSongs(int count, std::string& outError);
    std::vector<Song>    getAllSongs(std::string& outError);

    ArtistInfo            getArtistInfo(const std::string& artistId, std::string& outError);
    std::vector<Song>    getTopSongs(const std::string& artistName, int count,
                                     std::string& outError);
    Lyrics               getLyrics(const std::string& songId, const std::string& artist,
                                   const std::string& title, std::string& outError);

    bool setStarred(bool starred, const std::string& itemId, StarKind kind,
                    std::string& outError);
    bool setRating(int rating, const std::string& songId, std::string& outError);

    bool getSong(const std::string& songId, Song& out, std::string& outError);

    std::vector<Playlist> getPlaylists(std::string& outError);
    std::vector<Song>     getPlaylistSongs(const std::string& playlistId,
                                           std::string& outError);
    std::string createPlaylist(const std::string& name,
                               const std::vector<std::string>& songIds,
                               std::string& outError);
    bool addToPlaylist(const std::string& playlistId,
                       const std::vector<std::string>& songIds,
                       std::string& outError);
    bool removeFromPlaylist(const std::string& playlistId,
                            const std::vector<int>& indexes,
                            std::string& outError);
    bool renamePlaylist(const std::string& playlistId, const std::string& name,
                        std::string& outError);
    bool deletePlaylist(const std::string& playlistId, std::string& outError);

    std::vector<RadioStation> getRadioStations(std::string& outError);
    std::string createRadioStation(const std::string& streamUrl, const std::string& name,
                                   const std::string& homePageUrl, std::string& outError);
    bool updateRadioStation(const std::string& id, const std::string& streamUrl,
                            const std::string& name, const std::string& homePageUrl,
                            std::string& outError);
    bool deleteRadioStation(const std::string& id, std::string& outError);

    std::vector<PodcastChannel> getPodcastChannels(std::string& outError);
    std::vector<PodcastEpisode> getPodcastEpisodes(const std::string& channelId,
                                                    std::string& outError);
    std::string createPodcastChannel(const std::string& url, std::string& outError);
    bool        deletePodcastChannel(const std::string& id, std::string& outError);

    std::vector<NowPlayingEntry> getNowPlaying(std::string& outError);

    std::vector<Bookmark> getBookmarks(std::string& outError);
    bool createBookmark(const std::string& songId, double positionMs,
                        const std::string& comment, std::string& outError);
    bool deleteBookmark(const std::string& songId, std::string& outError);

    ScanStatus startScan(std::string& outError);
    ScanStatus getScanStatus(std::string& outError);

    bool scrobble(const std::string& songId, bool submission, std::string& outError);

    std::string streamURL(const std::string& songId);
    std::string downloadURL(const std::string& songId);
    std::string coverArtURL(const std::string& id, int size = 0);
    std::string coverArtURL(const SubsonicRequestContext& context,
                            const std::string& id, int size = 0) const;

    static std::vector<std::string> customHeaderLines();
    static std::wstring customHeadersWide();

    struct BinaryFetchResult {
        FetchClass cls;
        std::uint32_t httpStatus;
        std::string contentType;
        std::vector<std::uint8_t> body;
    };
    BinaryFetchResult httpGetBinary(const SubsonicRequestContext& context,
                                    const std::string& url,
                                    std::size_t maxBytes,
                                    class abort_callback& abort) const;

    bool httpDownloadToFile(const std::string& url, const std::wstring& destPath,
                            std::string& outError) const;

private:
    SubsonicClientWin();
    ~SubsonicClientWin();

    std::unique_ptr<IHttpTransport>    m_transport;
    std::unique_ptr<ISettingsProvider> m_settingsProvider;
    std::unique_ptr<SubsonicCore>      m_core;
};
}
