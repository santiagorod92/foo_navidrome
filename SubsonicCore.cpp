// Shared Subsonic API core — see SubsonicCore.h. Every request body lives here
// once; the Windows and macOS clients supply only an IHttpTransport (WinHTTP /
// NSURLSession) and an ISettingsProvider (the cfg_* globals).
//
// SDK-free: SubsonicTypes.h owns the json DOM, the object->struct parsers, the
// multi-library fan-out kernels and the retry policy; this file only assembles
// URLs, drives the retry loop and walks the parsed response. The one
// platform-specific line is the MD5 primitive (WinCrypt / CommonCrypto), the
// same split MediaEnrichmentLogic.cpp and SubsonicClient.mm already carry — kept
// local so the file is self-contained on every build path (MediaEnrichmentLogic
// isn't linked into the macOS component).

#if defined(_WIN32)
#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wincrypt.h>
#pragma comment(lib, "advapi32.lib")
#else
#include <CommonCrypto/CommonDigest.h>
#endif

#include "SubsonicCore.h"
#include "NavidromeDebugLog.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <random>
#include <thread>
#include <unordered_set>

namespace navidrome {

// ---------------------------------------------------------------------------
// IHttpTransport default backoff primitives
// ---------------------------------------------------------------------------
void IHttpTransport::sleepMs(int ms) {
    if (ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

int IHttpTransport::jitterMs() {
    static thread_local std::mt19937 rng(std::random_device{}());
    return static_cast<int>(std::uniform_int_distribution<unsigned>(0, 199)(rng));
}

// ---------------------------------------------------------------------------
// Local helpers
// ---------------------------------------------------------------------------
namespace {

#if defined(_WIN32)
std::string md5Hex(const std::string& input) {
    HCRYPTPROV prov = 0;
    HCRYPTHASH hash = 0;
    if (!CryptAcquireContextW(&prov, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
        return {};
    if (!CryptCreateHash(prov, CALG_MD5, 0, 0, &hash)) {
        CryptReleaseContext(prov, 0);
        return {};
    }
    const bool updated = CryptHashData(
        hash, reinterpret_cast<const BYTE*>(input.data()),
        static_cast<DWORD>(input.size()), 0) != FALSE;
    BYTE  digest[16] = {};
    DWORD digestSize = sizeof(digest);
    const bool read = updated &&
        CryptGetHashParam(hash, HP_HASHVAL, digest, &digestSize, 0) != FALSE;
    CryptDestroyHash(hash);
    CryptReleaseContext(prov, 0);
    if (!read || digestSize != sizeof(digest)) return {};
    char hex[33];
    for (int i = 0; i < 16; ++i) std::snprintf(hex + i * 2, 3, "%02x", digest[i]);
    return std::string(hex, 32);
}
#else
std::string md5Hex(const std::string& input) {
    unsigned char digest[CC_MD5_DIGEST_LENGTH] = {};
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    CC_MD5(input.data(), static_cast<CC_LONG>(input.size()), digest);
#pragma clang diagnostic pop
    char hex[CC_MD5_DIGEST_LENGTH * 2 + 1];
    for (int i = 0; i < CC_MD5_DIGEST_LENGTH; ++i)
        std::snprintf(hex + i * 2, 3, "%02x", digest[i]);
    return std::string(hex, CC_MD5_DIGEST_LENGTH * 2);
}
#endif

std::string enc(const std::string& s) { return percentEncode(s); }

}  // namespace

// ---------------------------------------------------------------------------
// SubsonicCore
// ---------------------------------------------------------------------------
SubsonicCore::SubsonicCore(IHttpTransport& transport, ISettingsProvider& settings)
    : m_http(transport), m_settings(settings) {}

std::string SubsonicCore::generateToken(const std::string& password, const std::string& salt) {
    return md5Hex(password + salt);
}

bool SubsonicCore::isConfigured() const {
    const SubsonicSettings s = m_settings.load();
    return !s.serverUrl.empty() && !s.username.empty() && !s.password.empty();
}

std::string SubsonicCore::authParams() const {
    const SubsonicSettings s = m_settings.load();
    const std::string token = md5Hex(s.password + s.salt);
    return "u=" + enc(s.username) + "&t=" + token + "&s=" + s.salt +
           "&v=1.16.1&c=foo_navidrome&f=json";
}

std::string SubsonicCore::buildURL(const std::string& endpoint, const std::string& extra) const {
    std::string base = m_settings.load().serverUrl;
    while (!base.empty() && base.back() == '/') base.pop_back();
    std::string url = base + "/rest/" + endpoint + "?" + authParams();
    if (!extra.empty()) url += "&" + extra;
    return url;
}

std::string SubsonicCore::streamURL(const std::string& songId, const std::string& coverArtId) const {
    const SubsonicSettings s = m_settings.load();
    std::string extra = "id=" + enc(songId);
    if (!coverArtId.empty()) extra += "&coverArt=" + enc(coverArtId);
    extra += streamTranscodeParams(s.streamFormat, s.maxBitrate);
    return buildURL("stream.view", extra);
}

std::string SubsonicCore::downloadURL(const std::string& songId) const {
    return buildURL("download.view", "id=" + enc(songId));
}

std::string SubsonicCore::coverArtURL(const std::string& id, int size) const {
    std::string extra = "id=" + enc(id);
    if (size > 0) extra += "&size=" + std::to_string(size);
    return buildURL("getCoverArt.view", extra);
}

// ---------------------------------------------------------------------------
// Transport + response wrapper
// ---------------------------------------------------------------------------
std::string SubsonicCore::httpGet(const std::string& url, std::string& outError) {
    const std::string safeUrl = dbg::scrubAuth(url);
    NAVIDROME_TIMER("HTTP", "GET " + safeUrl);
    NAVIDROME_LOG("HTTP", "GET " + safeUrl);
    m_lastError = Error{};

    HttpResult res;
    for (int attempt = 1; attempt <= retry::kMaxAttempts; ++attempt) {
        res = m_http.getOnce(url);
        if (res.error.ok() || !retry::again(res.error, attempt)) break;
        const int backoff = retry::backoffMs(attempt, m_http.jitterMs());
        NAVIDROME_WARN("HTTP", res.error.message + " — retry " + std::to_string(attempt + 1) +
                       "/" + std::to_string(retry::kMaxAttempts) + " in " +
                       std::to_string(backoff) + "ms  (" + safeUrl + ")");
        m_http.sleepMs(backoff);
    }

    m_lastError = res.error;
    if (!res.error.ok()) {
        outError = res.error.message;
        NAVIDROME_ERR("HTTP", std::string(res.error.kindName()) + ": " + res.error.message +
                      "  (" + safeUrl + ")");
        if (res.error.kind == ErrorKind::Auth) m_http.onAuthRejected();
        return {};
    }
    NAVIDROME_LOG("HTTP", "200 OK  " + std::to_string(res.body.size()) + " bytes");
    return res.body;
}

json::Value SubsonicCore::checkResponse(const std::string& body, std::string& outError) {
    SubsonicResponse resp = parseSubsonicResponse(body);
    if (!resp.ok) {
        m_lastError = resp.error;
        outError    = resp.error.message;
        if (resp.error.code != 0) {
            NAVIDROME_ERR("API", "Subsonic status != ok (code " +
                          std::to_string(resp.error.code) + ", " +
                          m_lastError.kindName() + "): " + outError);
        } else {
            NAVIDROME_ERR("API", std::string(m_lastError.kindName()) + ": " + outError);
        }
        if (resp.error.kind == ErrorKind::Auth) m_http.onAuthRejected();
        return json::Value{};
    }
    return resp.inner();   // copied out; caller owns it
}

bool SubsonicCore::ping(std::string& outError) {
    std::string body = httpGet(buildURL("ping.view"), outError);
    if (body.empty()) return false;
    return !checkResponse(body, outError).isNull();
}

// ---------------------------------------------------------------------------
// Music folders / multi-library filter
// ---------------------------------------------------------------------------
std::vector<MusicFolder> SubsonicCore::getMusicFolders(std::string& outError) {
    std::string body = httpGet(buildURL("getMusicFolders.view"), outError);
    if (body.empty()) return {};
    auto root = checkResponse(body, outError);
    if (root.isNull()) return {};

    std::vector<MusicFolder> result;
    for (auto* f : root["musicFolders"]["musicFolder"].items()) {
        MusicFolder mf = parseMusicFolder(*f);
        if (!mf.id.empty()) result.push_back(std::move(mf));
    }
    return result;
}

void SubsonicCore::refreshMusicFolders() {
    m_folderFetched = false;
    m_folderCache.clear();
}

std::vector<MusicFolder> SubsonicCore::cachedMusicFolders() {
    if (!m_folderFetched) {
        std::string err;
        auto folders = getMusicFolders(err);
        if (!folders.empty()) {          // latch only on a good answer; retry after a failure
            m_folderCache   = std::move(folders);
            m_folderFetched = true;
        }
    }
    return m_folderCache;
}

std::vector<std::string> SubsonicCore::activeMusicFolderIds() {
    const SubsonicSettings s = m_settings.load();
    if (!s.libraryFilter) return {};
    return effectiveMusicFolderIds(true, s.libraryIdsCsv, cachedMusicFolders());
}

std::vector<std::string> SubsonicCore::libraryGroupingIds() {
    auto folders = cachedMusicFolders();
    if (folders.size() < 2) return {};   // single-library server → flat list, always

    std::vector<std::string> allIds;
    for (auto& f : folders) allIds.push_back(f.id);

    // Grouping by library is independent of the "Only include selected
    // libraries" checkbox: a multi-library server always groups. The checkbox
    // only narrows *which* libraries show — and only when 2+ are ticked (1
    // ticked is a single-library scope, handled flat by activeMusicFolderIds()).
    const SubsonicSettings s = m_settings.load();
    if (s.libraryFilter) {
        auto sel = parseMusicFolderIds(s.libraryIdsCsv);
        std::vector<std::string> picked;
        for (auto& id : allIds)
            if (std::find(sel.begin(), sel.end(), id) != sel.end())
                picked.push_back(id);
        if (picked.size() >= 2) return picked;
        if (picked.size() == 1) return {};   // scoped to one library → flat
        // 0 ticked → fall through to "all libraries"
    }
    return allIds;
}

// ---------------------------------------------------------------------------
// Browse
// ---------------------------------------------------------------------------
std::vector<Artist> SubsonicCore::fetchArtistsForFolder(const std::string& folderId,
                                                        std::string& outError) {
    std::string body = httpGet(
        buildURL("getArtists.view", appendMusicFolderParam("", folderId)), outError);
    if (body.empty()) return {};
    auto root = checkResponse(body, outError);
    if (root.isNull()) return {};

    std::vector<Artist> result;
    for (auto* idxObj : root["artists"]["index"].items())
        for (auto* a : (*idxObj)["artist"].items())
            result.push_back(parseArtist(*a));
    return result;
}

std::vector<Artist> SubsonicCore::getArtists(std::string& outError) {
    auto fetch = [&](const std::string& folderId) {
        return fetchArtistsForFolder(folderId, outError);
    };
    return mergeFanOut<Artist>(activeMusicFolderIds(), fetch,
                               [](const Artist& a) { return a.id; });
}

std::vector<Artist> SubsonicCore::getArtistsForLibrary(const std::string& libraryId,
                                                       std::string& outError) {
    return fetchArtistsForFolder(libraryId, outError);
}

std::vector<Album> SubsonicCore::getAlbumsForArtist(const std::string& artistId,
                                                    std::string& outError,
                                                    const std::string& scopeLibraryId) {
    std::string body = httpGet(buildURL("getArtist.view", "id=" + enc(artistId)), outError);
    if (body.empty()) return {};
    auto root = checkResponse(body, outError);
    if (root.isNull()) return {};

    const json::Value& artistObj = root["artist"];
    const std::string artistName = jStr(artistObj, "name");

    std::vector<Album> result;
    for (auto* a : artistObj["album"].items()) {
        Album al = parseAlbum(*a);
        if (al.artistId.empty()) al.artistId = artistId;
        result.push_back(std::move(al));
    }

    // getArtist.view ignores musicFolderId server-side and AlbumID3 carries no
    // library id, so when the library filter is active we can't scope the album
    // list directly. search3.view *does* honor musicFolderId: fan it out over
    // the selected libraries, keep the album ids that belong to this artist, and
    // filter the getArtist.view list against that allow-set (order preserved).
    // scopeLibraryId pins the list to one library (a per-library tree node).
    const std::vector<std::string> folderIds =
        scopeLibraryId.empty() ? activeMusicFolderIds()
                               : std::vector<std::string>{ scopeLibraryId };
    if (folderIds.empty() || result.empty() || artistName.empty())
        return result;

    std::vector<Album> searchAlbums;
    const std::string searchBase = "query=" + enc(artistName) +
                                   "&artistCount=0&albumCount=500&songCount=0";
    for (const auto& fid : folderIds) {
        std::string sBody = httpGet(
            buildURL("search3.view", appendMusicFolderParam(searchBase, fid)), outError);
        if (sBody.empty()) continue;
        auto sRoot = checkResponse(sBody, outError);
        if (sRoot.isNull()) continue;
        for (auto* a : sRoot["searchResult3"]["album"].items())
            searchAlbums.push_back(parseAlbum(*a));
    }

    // The search passes are best-effort scoping; a failure there must not turn
    // into a user-visible error when getArtist.view itself succeeded.
    outError.clear();

    bool unconfirmed = false;
    std::vector<Album> out = filterAlbumsByArtistSearch(result, artistId, searchAlbums, unconfirmed);
    if (unconfirmed) {
        NAVIDROME_WARN("HTTP", "library filter: could not confirm album membership "
                       "for artist " + artistId + " — showing all albums");
    }
    return out;
}

std::vector<Song> SubsonicCore::getSongsForAlbum(const std::string& albumId, std::string& outError) {
    std::string body = httpGet(buildURL("getAlbum.view", "id=" + enc(albumId)), outError);
    if (body.empty()) return {};
    auto root = checkResponse(body, outError);
    if (root.isNull()) return {};

    std::vector<Song> result;
    for (auto* s : root["album"]["song"].items()) {
        Song so = parseSong(*s);
        if (so.albumId.empty()) so.albumId = albumId;
        result.push_back(std::move(so));
    }
    return result;
}

SearchResults SubsonicCore::search(const std::string& query, std::string& outError) {
    const std::string base = "query=" + enc(query) +
                             "&artistCount=20&albumCount=20&songCount=50";

    auto fetch = [&](const std::string& folderId) -> SearchResults {
        std::string body = httpGet(
            buildURL("search3.view", appendMusicFolderParam(base, folderId)), outError);
        if (body.empty()) return {};
        auto root = checkResponse(body, outError);
        if (root.isNull()) return {};

        SearchResults r;
        const json::Value& sr = root["searchResult3"];
        for (auto* a : sr["artist"].items()) r.artists.push_back(parseArtist(*a));
        for (auto* a : sr["album"].items())  r.albums.push_back(parseAlbum(*a));
        for (auto* s : sr["song"].items())   r.songs.push_back(parseSong(*s));
        return r;
    };

    const auto folderIds = activeMusicFolderIds();
    if (folderIds.empty()) return fetch(std::string());

    SearchResults merged;
    std::unordered_set<std::string> seenArtists, seenAlbums, seenSongs;
    for (const auto& fid : folderIds) {
        SearchResults part = fetch(fid);
        for (auto& a : part.artists)
            if (a.id.empty() || seenArtists.insert(a.id).second) merged.artists.push_back(std::move(a));
        for (auto& a : part.albums)
            if (a.id.empty() || seenAlbums.insert(a.id).second) merged.albums.push_back(std::move(a));
        for (auto& s : part.songs)
            if (s.id.empty() || seenSongs.insert(s.id).second) merged.songs.push_back(std::move(s));
    }
    return merged;
}

// ---------------------------------------------------------------------------
// Smart lists, favorites, ratings
// ---------------------------------------------------------------------------
std::vector<Album> SubsonicCore::getAlbumList(AlbumListType type, int size, std::string& outError) {
    const std::string base = std::string("type=") + albumListTypeName(type) +
                             "&size=" + std::to_string(size);
    auto fetch = [&](const std::string& folderId) -> std::vector<Album> {
        std::string body = httpGet(
            buildURL("getAlbumList2.view", appendMusicFolderParam(base, folderId)), outError);
        if (body.empty()) return {};
        auto root = checkResponse(body, outError);
        if (root.isNull()) return {};

        std::vector<Album> result;
        for (auto* a : root["albumList2"]["album"].items())
            result.push_back(parseAlbum(*a));
        return result;
    };
    return mergeFanOut<Album>(activeMusicFolderIds(), fetch,
                              [](const Album& a) { return a.id; });
}

std::vector<Song> SubsonicCore::getStarredSongs(std::string& outError) {
    auto fetch = [&](const std::string& folderId) -> std::vector<Song> {
        std::string body = httpGet(
            buildURL("getStarred2.view", appendMusicFolderParam("", folderId)), outError);
        if (body.empty()) return {};
        auto root = checkResponse(body, outError);
        if (root.isNull()) return {};

        std::vector<Song> result;
        for (auto* s : root["starred2"]["song"].items()) {
            Song so = parseSong(*s);
            so.starred = true;   // getStarred2 omits the per-item "starred" field
            result.push_back(std::move(so));
        }
        return result;
    };
    return mergeFanOut<Song>(activeMusicFolderIds(), fetch,
                             [](const Song& s) { return s.id; });
}

std::vector<Genre> SubsonicCore::getGenres(std::string& outError) {
    std::string body = httpGet(buildURL("getGenres.view"), outError);
    if (body.empty()) return {};
    auto root = checkResponse(body, outError);
    if (root.isNull()) return {};

    std::vector<Genre> result;
    for (auto* g : root["genres"]["genre"].items()) {
        Genre gen = parseGenre(*g);
        if (gen.name.empty()) continue;   // skip the empty "no genre" bucket
        result.push_back(std::move(gen));
    }
    return result;
}

std::vector<Song> SubsonicCore::getSongsForGenre(const std::string& genre, int count,
                                                 std::string& outError) {
    if (genre.empty()) return {};
    const std::string base = "genre=" + enc(genre) + "&count=" + std::to_string(count);
    auto fetch = [&](const std::string& folderId) -> std::vector<Song> {
        std::string body = httpGet(
            buildURL("getSongsByGenre.view", appendMusicFolderParam(base, folderId)), outError);
        if (body.empty()) return {};
        auto root = checkResponse(body, outError);
        if (root.isNull()) return {};

        std::vector<Song> result;
        for (auto* s : root["songsByGenre"]["song"].items())
            result.push_back(parseSong(*s));
        return result;
    };
    return mergeFanOut<Song>(activeMusicFolderIds(), fetch,
                             [](const Song& s) { return s.id; });
}

std::vector<Song> SubsonicCore::getSimilarSongs(const std::string& itemId, int count,
                                                std::string& outError) {
    if (itemId.empty()) return {};
    std::string body = httpGet(
        buildURL("getSimilarSongs2.view", "id=" + enc(itemId) + "&count=" + std::to_string(count)),
        outError);
    if (body.empty()) return {};
    auto root = checkResponse(body, outError);
    if (root.isNull()) return {};

    std::vector<Song> result;
    for (auto* s : root["similarSongs2"]["song"].items())
        result.push_back(parseSong(*s));
    return result;
}

std::vector<Song> SubsonicCore::getRandomSongs(int count, std::string& outError) {
    const auto folderIds = activeMusicFolderIds();
    // Split the requested size across the fanned-out libraries so the merged
    // result stays near `count` rather than count-per-library.
    const int perFolder = folderIds.empty()
        ? count
        : (std::max)(1, count / static_cast<int>(folderIds.size()) + 1);

    auto fetch = [&](const std::string& folderId) -> std::vector<Song> {
        std::string body = httpGet(
            buildURL("getRandomSongs.view",
                     appendMusicFolderParam("size=" + std::to_string(perFolder), folderId)),
            outError);
        if (body.empty()) return {};
        auto root = checkResponse(body, outError);
        if (root.isNull()) return {};

        std::vector<Song> result;
        for (auto* s : root["randomSongs"]["song"].items())
            result.push_back(parseSong(*s));
        return result;
    };

    auto merged = mergeFanOut<Song>(folderIds, fetch, [](const Song& s) { return s.id; });
    if (!folderIds.empty() && static_cast<int>(merged.size()) > count)
        merged.resize(count);
    return merged;
}

bool SubsonicCore::setStarred(bool starred, const std::string& itemId, StarKind kind,
                              std::string& outError) {
    if (itemId.empty()) return false;
    std::string params = std::string(starParamName(kind)) + "=" + enc(itemId);
    std::string body = httpGet(buildURL(starred ? "star.view" : "unstar.view", params), outError);
    if (body.empty()) return false;
    return !checkResponse(body, outError).isNull();
}

bool SubsonicCore::setRating(int rating, const std::string& songId, std::string& outError) {
    if (songId.empty()) return false;
    if (rating < 0) rating = 0;
    if (rating > 5) rating = 5;
    std::string params = "id=" + enc(songId) + "&rating=" + std::to_string(rating);
    std::string body = httpGet(buildURL("setRating.view", params), outError);
    if (body.empty()) return false;
    return !checkResponse(body, outError).isNull();
}

bool SubsonicCore::getSong(const std::string& songId, Song& out, std::string& outError) {
    if (songId.empty()) return false;
    std::string body = httpGet(buildURL("getSong.view", "id=" + enc(songId)), outError);
    if (body.empty()) return false;
    auto root = checkResponse(body, outError);
    if (root.isNull()) return false;
    // getSong returns a bare "song" object; items() wraps it as a 1-element list.
    auto songs = root["song"].items();
    if (songs.empty()) return false;
    out = parseSong(*songs.front());
    return true;
}

// ---------------------------------------------------------------------------
// Server-side playlists
// ---------------------------------------------------------------------------
std::vector<Playlist> SubsonicCore::getPlaylists(std::string& outError) {
    std::string body = httpGet(buildURL("getPlaylists.view"), outError);
    if (body.empty()) return {};
    auto root = checkResponse(body, outError);
    if (root.isNull()) return {};

    std::vector<Playlist> result;
    for (auto* p : root["playlists"]["playlist"].items())
        result.push_back(parsePlaylist(*p));
    return result;
}

std::vector<Song> SubsonicCore::getPlaylistSongs(const std::string& playlistId, std::string& outError) {
    std::string body = httpGet(buildURL("getPlaylist.view", "id=" + enc(playlistId)), outError);
    if (body.empty()) return {};
    auto root = checkResponse(body, outError);
    if (root.isNull()) return {};

    std::vector<Song> result;
    for (auto* s : root["playlist"]["entry"].items())
        result.push_back(parseSong(*s));
    return result;
}

std::string SubsonicCore::createPlaylist(const std::string& name,
                                         const std::vector<std::string>& songIds,
                                         std::string& outError) {
    if (name.empty()) return "";
    constexpr std::size_t kChunk = kPlaylistChunkSize;

    std::size_t first = (std::min)(kChunk, songIds.size());
    std::string params = "name=" + enc(name);
    for (std::size_t i = 0; i < first; ++i) params += "&songId=" + enc(songIds[i]);

    std::string body = httpGet(buildURL("createPlaylist.view", params), outError);
    if (body.empty()) return "";
    auto root = checkResponse(body, outError);
    if (root.isNull()) return "";

    std::string playlistId;
    auto created = root["playlist"].items();
    if (!created.empty()) playlistId = jId(*created[0], "id");

    if (playlistId.empty()) {
        if (songIds.size() > kChunk) {
            outError = "Playlist created, but the server returned no id — "
                       "only the first " + std::to_string(kChunk) + " tracks were added";
        }
        // Everything made it in; we just have no id to hand back. outError stays
        // empty so the caller can tell this apart from a real failure.
        return "";
    }

    if (songIds.size() <= kChunk) return playlistId;

    std::vector<std::string> rest(songIds.begin() + kChunk, songIds.end());
    if (!addToPlaylist(playlistId, rest, outError)) return "";
    return playlistId;
}

bool SubsonicCore::addToPlaylist(const std::string& playlistId,
                                 const std::vector<std::string>& songIds,
                                 std::string& outError) {
    if (playlistId.empty() || songIds.empty()) return false;
    constexpr std::size_t kChunk = kPlaylistChunkSize;
    const std::size_t chunks = (songIds.size() + kChunk - 1) / kChunk;
    NAVIDROME_LOG("Playlist", "add " + std::to_string(songIds.size()) + " ids to " +
                  playlistId + " in " + std::to_string(chunks) + " chunk(s)");

    for (std::size_t i = 0, c = 1; i < songIds.size(); i += kChunk, ++c) {
        std::string upd = "playlistId=" + enc(playlistId);
        for (std::size_t j = i; j < (std::min)(i + kChunk, songIds.size()); ++j)
            upd += "&songIdToAdd=" + enc(songIds[j]);
        std::string body = httpGet(buildURL("updatePlaylist.view", upd), outError);
        if (body.empty() || checkResponse(body, outError).isNull()) {
            NAVIDROME_ERR("Playlist", "add: chunk " + std::to_string(c) + "/" +
                          std::to_string(chunks) + " failed after " + std::to_string(i) +
                          "/" + std::to_string(songIds.size()) + " ids: " + outError);
            return false;
        }
    }
    return true;
}

bool SubsonicCore::removeFromPlaylist(const std::string& playlistId,
                                      const std::vector<int>& indexes,
                                      std::string& outError) {
    if (playlistId.empty() || indexes.empty()) return false;
    constexpr std::size_t kChunk = kPlaylistChunkSize;

    std::vector<int> sorted = indexes;
    std::sort(sorted.begin(), sorted.end(), std::greater<int>());
    const std::size_t chunks = (sorted.size() + kChunk - 1) / kChunk;
    NAVIDROME_LOG("Playlist", "remove " + std::to_string(sorted.size()) +
                  " index(es) from " + playlistId + " (highest-first) in " +
                  std::to_string(chunks) + " chunk(s)");

    for (std::size_t i = 0, c = 1; i < sorted.size(); i += kChunk, ++c) {
        std::string upd = "playlistId=" + enc(playlistId);
        for (std::size_t j = i; j < (std::min)(i + kChunk, sorted.size()); ++j)
            upd += "&songIndexToRemove=" + std::to_string(sorted[j]);
        std::string body = httpGet(buildURL("updatePlaylist.view", upd), outError);
        if (body.empty() || checkResponse(body, outError).isNull()) {
            NAVIDROME_ERR("Playlist", "remove: chunk " + std::to_string(c) + "/" +
                          std::to_string(chunks) + " failed after " + std::to_string(i) +
                          "/" + std::to_string(sorted.size()) + " indexes: " + outError);
            return false;
        }
    }
    return true;
}

bool SubsonicCore::renamePlaylist(const std::string& playlistId, const std::string& name,
                                  std::string& outError) {
    if (playlistId.empty() || name.empty()) return false;
    std::string params = "playlistId=" + enc(playlistId) + "&name=" + enc(name);
    std::string body = httpGet(buildURL("updatePlaylist.view", params), outError);
    if (body.empty()) return false;
    return !checkResponse(body, outError).isNull();
}

bool SubsonicCore::deletePlaylist(const std::string& playlistId, std::string& outError) {
    if (playlistId.empty()) return false;
    std::string body = httpGet(buildURL("deletePlaylist.view", "id=" + enc(playlistId)), outError);
    if (body.empty()) return false;
    return !checkResponse(body, outError).isNull();
}

// ---------------------------------------------------------------------------
// Internet radio
// ---------------------------------------------------------------------------
std::vector<RadioStation> SubsonicCore::getRadioStations(std::string& outError) {
    std::string body = httpGet(buildURL("getInternetRadioStations.view"), outError);
    if (body.empty()) return {};
    auto root = checkResponse(body, outError);
    if (root.isNull()) return {};

    std::vector<RadioStation> result;
    for (auto* s : root["internetRadioStations"]["internetRadioStation"].items())
        result.push_back(parseRadioStation(*s));
    return result;
}

std::string SubsonicCore::createRadioStation(const std::string& streamUrl, const std::string& name,
                                             const std::string& homePageUrl, std::string& outError) {
    if (streamUrl.empty() || name.empty()) return "";
    std::string params = "streamUrl=" + enc(streamUrl) + "&name=" + enc(name);
    if (!homePageUrl.empty()) params += "&homePageUrl=" + enc(homePageUrl);

    std::string body = httpGet(buildURL("createInternetRadioStation.view", params), outError);
    if (body.empty()) return "";
    if (checkResponse(body, outError).isNull()) return "";
    // Unlike createPlaylist.view, Subsonic's create-station endpoint doesn't echo
    // the new station's id back. Report success with an empty id rather than a
    // phantom failure — callers must check outError, not this string.
    return "";
}

bool SubsonicCore::updateRadioStation(const std::string& id, const std::string& streamUrl,
                                      const std::string& name, const std::string& homePageUrl,
                                      std::string& outError) {
    if (id.empty() || streamUrl.empty() || name.empty()) return false;
    std::string params = "id=" + enc(id) + "&streamUrl=" + enc(streamUrl) + "&name=" + enc(name);
    if (!homePageUrl.empty()) params += "&homePageUrl=" + enc(homePageUrl);
    std::string body = httpGet(buildURL("updateInternetRadioStation.view", params), outError);
    if (body.empty()) return false;
    return !checkResponse(body, outError).isNull();
}

bool SubsonicCore::deleteRadioStation(const std::string& id, std::string& outError) {
    if (id.empty()) return false;
    std::string body = httpGet(buildURL("deleteInternetRadioStation.view", "id=" + enc(id)), outError);
    if (body.empty()) return false;
    return !checkResponse(body, outError).isNull();
}

// ---------------------------------------------------------------------------
// Bookmarks
// ---------------------------------------------------------------------------
std::vector<Bookmark> SubsonicCore::getBookmarks(std::string& outError) {
    std::string body = httpGet(buildURL("getBookmarks.view"), outError);
    if (body.empty()) return {};
    auto root = checkResponse(body, outError);
    if (root.isNull()) return {};

    std::vector<Bookmark> result;
    for (auto* b : root["bookmarks"]["bookmark"].items()) {
        Bookmark bm;
        if (parseBookmark(*b, bm)) result.push_back(std::move(bm));
    }
    return result;
}

bool SubsonicCore::createBookmark(const std::string& songId, double positionMs,
                                  const std::string& comment, std::string& outError) {
    if (songId.empty()) return false;
    std::string params = "id=" + enc(songId) +
                         "&position=" + std::to_string(static_cast<long long>(positionMs));
    if (!comment.empty()) params += "&comment=" + enc(comment);
    std::string body = httpGet(buildURL("createBookmark.view", params), outError);
    if (body.empty()) return false;
    return !checkResponse(body, outError).isNull();
}

bool SubsonicCore::deleteBookmark(const std::string& songId, std::string& outError) {
    if (songId.empty()) return false;
    std::string body = httpGet(buildURL("deleteBookmark.view", "id=" + enc(songId)), outError);
    if (body.empty()) return false;
    return !checkResponse(body, outError).isNull();
}

// ---------------------------------------------------------------------------
// Library scan
// ---------------------------------------------------------------------------
ScanStatus SubsonicCore::startScan(std::string& outError) {
    std::string body = httpGet(buildURL("startScan.view"), outError);
    if (body.empty()) return {};
    auto root = checkResponse(body, outError);
    if (root.isNull()) return {};
    return parseScanStatus(root);
}

ScanStatus SubsonicCore::getScanStatus(std::string& outError) {
    std::string body = httpGet(buildURL("getScanStatus.view"), outError);
    if (body.empty()) return {};
    auto root = checkResponse(body, outError);
    if (root.isNull()) return {};
    return parseScanStatus(root);
}

// ---------------------------------------------------------------------------
// Scrobble
// ---------------------------------------------------------------------------
bool SubsonicCore::scrobble(const std::string& songId, bool submission, std::string& outError) {
    if (songId.empty()) return false;
    std::string params = "id=" + enc(songId) + "&submission=" + (submission ? "true" : "false");
    std::string body = httpGet(buildURL("scrobble.view", params), outError);
    if (body.empty()) return false;
    return !checkResponse(body, outError).isNull();
}

}  // namespace navidrome
