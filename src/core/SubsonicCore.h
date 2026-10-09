#pragma once
#include "SubsonicTypes.h"
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace navidrome {

struct HttpResult {
    std::string body;
    Error       error;
};

struct IHttpTransport {
    virtual ~IHttpTransport() = default;

    virtual HttpResult getOnce(const std::string& url) = 0;

    virtual void onAuthRejected() {}

    virtual void sleepMs(int ms);
    virtual int  jitterMs();
};

struct SubsonicSettings {
    std::string serverUrl;
    std::string username;
    std::string password;
    std::string salt;
    std::string streamFormat;
    int         maxBitrate = 0;
    bool        libraryFilter = false;
    std::string libraryIdsCsv;
};

struct ISettingsProvider {
    virtual ~ISettingsProvider() = default;
    virtual SubsonicSettings load() const = 0;
};

class SubsonicCore {
public:
    SubsonicCore(IHttpTransport& transport, ISettingsProvider& settings);

    bool         isConfigured() const;
    const Error& lastError() const { return m_lastError; }
    bool         ping(std::string& outError);
    bool         serverInfo(ServerInfo& out, std::string& outError);
    bool         capabilities(ServerInfo& out);

    std::vector<MusicFolder>  getMusicFolders(std::string& outError);
    std::vector<MusicFolder>  cachedMusicFolders();
    void                      refreshMusicFolders();
    std::vector<std::string>  activeMusicFolderIds();
    std::vector<std::string>  libraryGroupingIds();

    std::vector<Artist> getArtists(std::string& outError);
    std::vector<Artist> getArtistsForLibrary(const std::string& libraryId, std::string& outError);
    std::vector<Album>  getAlbumsForArtist(const std::string& artistId, std::string& outError,
                                           const std::string& scopeLibraryId = "");
    std::vector<Song>   getSongsForAlbum(const std::string& albumId, std::string& outError);
    SearchResults       search(const std::string& query, std::string& outError);

    std::vector<Album> getAlbumList(AlbumListType type, int size, std::string& outError);
    std::vector<Song>  getStarredSongs(std::string& outError);
    std::vector<Genre> getGenres(std::string& outError);
    std::vector<Song>  getSongsForGenre(const std::string& genre, int count, std::string& outError);
    std::vector<Song>  getSimilarSongs(const std::string& itemId, int count, std::string& outError);
    std::vector<Song>  getRandomSongs(int count, std::string& outError);
    std::vector<Song>  getAllSongs(std::string& outError, int pageSize = kAllSongsPageSize);
    static constexpr int kAllSongsPageSize = 500;
    ArtistInfo         getArtistInfo(const std::string& artistId, std::string& outError);
    std::vector<Song>  getTopSongs(const std::string& artistName, int count, std::string& outError);
    Lyrics             getLyrics(const std::string& songId, const std::string& artist,
                                 const std::string& title, std::string& outError);
    bool setStarred(bool starred, const std::string& itemId, StarKind kind, std::string& outError);
    bool setRating(int rating, const std::string& songId, std::string& outError);
    bool getSong(const std::string& songId, Song& out, std::string& outError);

    std::vector<Playlist> getPlaylists(std::string& outError);
    std::vector<Song>     getPlaylistSongs(const std::string& playlistId, std::string& outError);
    std::string createPlaylist(const std::string& name, const std::vector<std::string>& songIds,
                               std::string& outError);
    bool addToPlaylist(const std::string& playlistId, const std::vector<std::string>& songIds,
                       std::string& outError);
    bool removeFromPlaylist(const std::string& playlistId, const std::vector<int>& indexes,
                            std::string& outError);
    bool renamePlaylist(const std::string& playlistId, const std::string& name, std::string& outError);
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
    bool createBookmark(const std::string& songId, double positionMs, const std::string& comment,
                        std::string& outError);
    bool deleteBookmark(const std::string& songId, std::string& outError);

    ScanStatus startScan(std::string& outError);
    ScanStatus getScanStatus(std::string& outError);

    bool scrobble(const std::string& songId, bool submission, std::string& outError);

    std::string authParams() const;
    std::string buildURL(const std::string& endpoint, const std::string& extra = "") const;
    std::string streamURL(const std::string& songId, const std::string& coverArtId = "") const;
    std::string downloadURL(const std::string& songId) const;
    std::string coverArtURL(const std::string& id, int size = 0) const;

    static std::string generateToken(const std::string& password, const std::string& salt);

private:
    std::string httpGet(const std::string& url, std::string& outError);
    json::Value checkResponse(const std::string& body, std::string& outError);
    std::vector<Artist> fetchArtistsForFolder(const std::string& folderId, std::string& outError);

    IHttpTransport&    m_http;
    ISettingsProvider& m_settings;
    Error                    m_lastError;
    std::vector<MusicFolder> m_folderCache;
    bool                     m_folderFetched = false;
    std::mutex               m_lyricsMutex;
    std::string              m_lyricsByIdUnsupportedOn;
    std::mutex               m_capsMutex;
    std::string              m_capsServer;
    ServerInfo               m_caps;
};
}
