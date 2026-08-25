#pragma once
#include "../SubsonicTypes.h"
#include "MediaEnrichmentLogic.h"
#include <cstdint>
#include <string>
#include <vector>

namespace navidrome {

// Snapshot of the credentials/config needed to make a Subsonic request,
// captured once up front so background work (cover art fetches on a
// abort_callback thread, ESLyric config generation) isn't racing live edits
// to the cfg_string globals.
struct SubsonicRequestContext {
    std::string serverUrl;
    std::string username;
    std::string password;
    std::string salt;
    std::string customHeaders;
};

// Windows Subsonic API client (WinHTTP-based).
// Mirrors the ObjC SubsonicClient used on macOS.
class SubsonicClientWin {
public:
    static SubsonicClientWin& get();

    bool isConfigured() const;
    SubsonicRequestContext snapshot() const;
    bool ping(std::string& outError);

    std::vector<Artist>  getArtists(std::string& outError);
    std::vector<Album>   getAlbumsForArtist(const std::string& artistId, std::string& outError);
    std::vector<Song>    getSongsForAlbum(const std::string& albumId, std::string& outError);
    SearchResults        search(const std::string& query, std::string& outError);

    // Smart lists — getAlbumList2.view. Backs the browser's category nodes.
    std::vector<Album>   getAlbumList(AlbumListType type, int size, std::string& outError);
    // Starred tracks (getStarred2.view).
    std::vector<Song>    getStarredSongs(std::string& outError);

    // Genres (getGenres.view) and their tracks (getSongsByGenre.view). Back the
    // browser's "Genres" category node.
    std::vector<Genre>   getGenres(std::string& outError);
    std::vector<Song>    getSongsForGenre(const std::string& genre, int count,
                                          std::string& outError);

    // Favorites + ratings. Per-user server-side state, so it shows up in the
    // Navidrome web UI and every other Subsonic client.
    bool setStarred(bool starred, const std::string& itemId, StarKind kind,
                    std::string& outError);
    // rating 1-5; 0 clears the rating.
    bool setRating(int rating, const std::string& songId, std::string& outError);

    // Server-side playlists
    std::vector<Playlist> getPlaylists(std::string& outError);
    std::vector<Song>     getPlaylistSongs(const std::string& playlistId,
                                           std::string& outError);
    // Creates a playlist and returns its id ("" on failure — check outError,
    // which stays empty when the server just didn't echo an id back).
    // songIds may be empty to create an empty playlist.
    std::string createPlaylist(const std::string& name,
                               const std::vector<std::string>& songIds,
                               std::string& outError);
    // Appends songs to an existing playlist (updatePlaylist.view songIdToAdd).
    bool addToPlaylist(const std::string& playlistId,
                       const std::vector<std::string>& songIds,
                       std::string& outError);
    // Removes entries by their zero-based position. Indexes are applied
    // highest-first so earlier removals can't shift the later ones.
    bool removeFromPlaylist(const std::string& playlistId,
                            const std::vector<int>& indexes,
                            std::string& outError);
    bool renamePlaylist(const std::string& playlistId, const std::string& name,
                        std::string& outError);
    bool deletePlaylist(const std::string& playlistId, std::string& outError);

    // Internet radio stations (getInternetRadioStations.view + CRUD). Playback
    // uses RadioStation::streamUrl directly.
    std::vector<RadioStation> getRadioStations(std::string& outError);
    // Creates a station. Subsonic's create endpoint doesn't echo the new
    // station's id back (unlike createPlaylist.view), so this returns "" on
    // success — check outError, not the returned string.
    std::string createRadioStation(const std::string& streamUrl, const std::string& name,
                                   const std::string& homePageUrl, std::string& outError);
    bool updateRadioStation(const std::string& id, const std::string& streamUrl,
                            const std::string& name, const std::string& homePageUrl,
                            std::string& outError);
    bool deleteRadioStation(const std::string& id, std::string& outError);

    // Saved resume positions (getBookmarks.view). createBookmark is an upsert —
    // Subsonic overwrites any existing bookmark for the same song.
    std::vector<Bookmark> getBookmarks(std::string& outError);
    bool createBookmark(const std::string& songId, double positionMs,
                        const std::string& comment, std::string& outError);
    bool deleteBookmark(const std::string& songId, std::string& outError);

    // Scrobble a play: submission=false marks "now playing", submission=true
    // registers the play (play count, Last.fm / ListenBrainz relay).
    bool scrobble(const std::string& songId, bool submission, std::string& outError);

    // Carries the configured transcoding preferences (format / maxBitRate).
    std::string streamURL(const std::string& songId);
    // download.view — always the original file, never transcoded.
    std::string downloadURL(const std::string& songId);
    std::string coverArtURL(const std::string& id, int size = 0);
    std::string coverArtURL(const SubsonicRequestContext& context,
                            const std::string& id, int size = 0) const;

    // User-configured extra HTTP headers ("Name: Value" lines) applied to every
    // request — API, cover art and audio stream. Shared so the WinHTTP clients
    // and the navidrome:// input handler all send the same set.
    static std::vector<std::string> customHeaderLines();
    // Same headers joined as a single CRLF-delimited wide string for
    // WinHttpAddRequestHeaders (empty if none configured).
    static std::wstring customHeadersWide();

    // Generate Subsonic token from password + salt (md5(password + salt))
    static std::string generateToken(const std::string& password, const std::string& salt);

    // Binary fetch for cover art: reads the whole body only on HTTP 200 and a
    // recognized image payload, capped at maxBytes, honoring abort_callback.
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

    // Streams a URL straight to disk — no size cap and no content sniffing, so
    // it suits full-quality track downloads that must not sit in memory.
    // destPath is a native wide path; the file is replaced if it exists.
    bool httpDownloadToFile(const std::string& url, const std::wstring& destPath,
                            std::string& outError) const;

private:
    SubsonicClientWin() = default;

    std::string authParams() const;
    std::string buildURL(const std::string& endpoint, const std::string& extra = "") const;
    // Synchronous HTTP GET; returns body or "" on error (sets outError).
    std::string httpGet(const std::string& url, std::string& outError) const;
};

} // namespace navidrome
