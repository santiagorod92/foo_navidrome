#pragma once
// Shared Subsonic API core — one implementation of every request body (URL
// assembly, the retry loop, the status-wrapper check, the json::Value walk and
// the multi-library fan-out), written once so the Windows (WinHTTP) and macOS
// (NSURLSession) clients are thin transport + view-model adapters over it.
//
// SDK-free on purpose: it #includes only SubsonicTypes.h (+ the header-only
// debug tracer), so it compiles into the component on every build path AND into
// the tests/ host, where it is exercised with a fake transport (testSubsonicCore).
#include "SubsonicTypes.h"
#include <memory>
#include <string>
#include <vector>

namespace navidrome {

// The outcome of a single HTTP GET attempt. The retry loop lives in
// SubsonicCore, so this is one send/receive: `body` holds the response bytes
// when `error.ok()`, otherwise `error` is the classified failure.
struct HttpResult {
    std::string body;
    Error       error;
};

// What SubsonicCore needs from the platform's HTTP stack. `getOnce` is the only
// required method; the rest have defaults that suit the tests and either client.
struct IHttpTransport {
    virtual ~IHttpTransport() = default;

    // Perform one GET of `url` with the platform's configured timeouts, TLS
    // policy and custom headers. Never retries — SubsonicCore does that.
    virtual HttpResult getOnce(const std::string& url) = 0;

    // Called once (per SubsonicCore instance) the first time a request is
    // classified as ErrorKind::Auth, so the platform can surface the "server
    // rejected the credentials" notice to the user.
    virtual void onAuthRejected() {}

    // Backoff primitives — overridable if a platform prefers its native ones.
    virtual void sleepMs(int ms);
    virtual int  jitterMs();   // a fresh value in [0, 200)
};

// The subset of the cfg_* globals the core reads. Loaded fresh for every
// request (via ISettingsProvider) so a credential / server-URL rotation takes
// effect without recreating the core. `salt` already has its default applied by
// the provider and is never empty here.
struct SubsonicSettings {
    std::string serverUrl;
    std::string username;
    std::string password;
    std::string salt;
    std::string streamFormat;   // cfg_stream_format ("" / "raw" / "mp3" / …)
    int         maxBitrate = 0; // cfg_max_bitrate, kbps (0 = unlimited)
    bool        libraryFilter = false;   // cfg_library_filter
    std::string libraryIdsCsv;           // cfg_library_ids
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

    // --- Music folders / multi-library filter ------------------------------
    std::vector<MusicFolder>  getMusicFolders(std::string& outError);
    std::vector<MusicFolder>  cachedMusicFolders();   // fetch-once, session-cached
    void                      refreshMusicFolders();
    std::vector<std::string>  activeMusicFolderIds();   // {} => one request, no musicFolderId
    std::vector<std::string>  libraryGroupingIds();     // 2+ => browser groups by library

    // --- Browse ----------------------------------------------------------
    std::vector<Artist> getArtists(std::string& outError);
    std::vector<Artist> getArtistsForLibrary(const std::string& libraryId, std::string& outError);
    std::vector<Album>  getAlbumsForArtist(const std::string& artistId, std::string& outError,
                                           const std::string& scopeLibraryId = "");
    std::vector<Song>   getSongsForAlbum(const std::string& albumId, std::string& outError);
    SearchResults       search(const std::string& query, std::string& outError);

    // --- Smart lists / favorites / ratings ------------------------------
    std::vector<Album> getAlbumList(AlbumListType type, int size, std::string& outError);
    std::vector<Song>  getStarredSongs(std::string& outError);
    std::vector<Genre> getGenres(std::string& outError);
    std::vector<Song>  getSongsForGenre(const std::string& genre, int count, std::string& outError);
    std::vector<Song>  getSimilarSongs(const std::string& itemId, int count, std::string& outError);
    std::vector<Song>  getRandomSongs(int count, std::string& outError);
    bool setStarred(bool starred, const std::string& itemId, StarKind kind, std::string& outError);
    bool setRating(int rating, const std::string& songId, std::string& outError);
    bool getSong(const std::string& songId, Song& out, std::string& outError);

    // --- Server-side playlists -----------------------------------------
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

    // --- Internet radio ------------------------------------------------
    std::vector<RadioStation> getRadioStations(std::string& outError);
    std::string createRadioStation(const std::string& streamUrl, const std::string& name,
                                   const std::string& homePageUrl, std::string& outError);
    bool updateRadioStation(const std::string& id, const std::string& streamUrl,
                            const std::string& name, const std::string& homePageUrl,
                            std::string& outError);
    bool deleteRadioStation(const std::string& id, std::string& outError);

    // --- Bookmarks ---------------------------------------------------
    std::vector<Bookmark> getBookmarks(std::string& outError);
    bool createBookmark(const std::string& songId, double positionMs, const std::string& comment,
                        std::string& outError);
    bool deleteBookmark(const std::string& songId, std::string& outError);

    // --- Library scan ----------------------------------------------
    ScanStatus startScan(std::string& outError);
    ScanStatus getScanStatus(std::string& outError);

    // --- Scrobble ------------------------------------------------
    bool scrobble(const std::string& songId, bool submission, std::string& outError);

    // --- URL builders (no network) -------------------------------
    // `coverArtId` only affects streamURL: macOS embeds it as a query param so
    // the art extractor can pull it from the playing item's path, Windows passes "".
    std::string authParams() const;
    std::string buildURL(const std::string& endpoint, const std::string& extra = "") const;
    std::string streamURL(const std::string& songId, const std::string& coverArtId = "") const;
    std::string downloadURL(const std::string& songId) const;
    std::string coverArtURL(const std::string& id, int size = 0) const;

    // md5(password + salt) — the Subsonic auth token.
    static std::string generateToken(const std::string& password, const std::string& salt);

private:
    // GET with the shared retry policy (navidrome::retry); "" + outError on
    // failure, and m_lastError set to the classified outcome.
    std::string httpGet(const std::string& url, std::string& outError);
    // Parse the body, validate the subsonic-response status wrapper, hand back
    // the inner object (Null json::Value on any failure).
    json::Value checkResponse(const std::string& body, std::string& outError);
    // One getArtists.view response; folderId empty => no musicFolderId param.
    std::vector<Artist> fetchArtistsForFolder(const std::string& folderId, std::string& outError);

    IHttpTransport&    m_http;
    ISettingsProvider& m_settings;
    Error                    m_lastError;
    std::vector<MusicFolder> m_folderCache;
    bool                     m_folderFetched = false;
};

}  // namespace navidrome
