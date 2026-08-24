#include "stdafx.h"
#include "SubsonicClientWin.h"
#include "MediaEnrichmentLogic.h"
#include <SDK/cfg_var.h>
#include <algorithm>
#include <functional>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "crypt32.lib")

// Config vars defined in NavidromePluginWin.cpp
namespace navidrome {
    extern cfg_string cfg_server_url;
    extern cfg_string cfg_username;
    extern cfg_string cfg_password;
    extern cfg_string cfg_salt;
    extern cfg_string cfg_custom_headers;
    extern cfg_string cfg_stream_format;
    extern cfg_var_modern::cfg_int cfg_max_bitrate;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Force modern TLS on a WinHTTP session. WinHTTP's legacy default negotiates
// SSL3 / TLS1.0, which Cloudflare and most modern endpoints reject (handshake
// fails with ERROR_WINHTTP_SECURE_CHANNEL_ERROR, 12157). We offer only TLS
// 1.2 + 1.3 — secure and correct for real Windows schannel.
//
// NOTE (Wine only): a server configured with Minimum TLS Version = 1.3 still
// fails under Wine, because Wine's gnutls-backed schannel mis-negotiates when
// 1.2 and 1.3 are both offered (server replies fatal alert 70, protocol
// version). Real Windows schannel handles this fine; the workaround for Wine
// testing is to set the Cloudflare zone's Minimum TLS Version to 1.2.
static void applySecureProtocols(HINTERNET hSession) {
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
    protocols |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
    WinHttpSetOption(hSession, WINHTTP_OPTION_SECURE_PROTOCOLS,
                     &protocols, sizeof(protocols));
}

static std::wstring toWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    if (!w.empty() && w.back() == 0) w.pop_back();
    return w;
}

static std::string toUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
    if (!s.empty() && s.back() == 0) s.pop_back();
    return s;
}

static std::string md5hex(const std::string& input) {
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    if (!CryptAcquireContextW(&hProv, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
        return "";
    CryptCreateHash(hProv, CALG_MD5, 0, 0, &hHash);
    CryptHashData(hHash, reinterpret_cast<const BYTE*>(input.c_str()),
                  static_cast<DWORD>(input.size()), 0);
    DWORD len = 16;
    BYTE  digest[16] = {};
    CryptGetHashParam(hHash, HP_HASHVAL, digest, &len, 0);
    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);
    char hex[33];
    for (int i = 0; i < 16; i++) sprintf_s(hex + i * 2, 3, "%02x", digest[i]);
    return std::string(hex, 32);
}

static std::string urlEncode(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            out += static_cast<char>(c);
        else { char buf[4]; sprintf_s(buf, "%%%02X", c); out += buf; }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Minimal JSON extraction (Subsonic-specific, not a general parser)
// ---------------------------------------------------------------------------

// Extract first string value for "key":"value"
static std::string jstr(const std::string& s, const std::string& key,
                        const std::string& def = "") {
    auto k = "\"" + key + "\":\"";
    auto p = s.find(k);
    if (p == std::string::npos) return def;
    p += k.size();
    std::string val;
    for (; p < s.size() && s[p] != '"'; ++p) {
        if (s[p] == '\\' && p + 1 < s.size()) { ++p; val += s[p]; }
        else val += s[p];
    }
    return val;
}

// Extract first integer for "key":123
static int jint(const std::string& s, const std::string& key, int def = 0) {
    auto k = "\"" + key + "\":";
    auto p = s.find(k);
    if (p == std::string::npos) return def;
    p += k.size();
    while (p < s.size() && s[p] == ' ') ++p;
    if (p >= s.size() || (!isdigit(static_cast<unsigned char>(s[p])) && s[p] != '-'))
        return def;
    return atoi(s.c_str() + p);
}

// Extract first double for "key":1.5
static double jdbl(const std::string& s, const std::string& key, double def = 0.0) {
    auto k = "\"" + key + "\":";
    auto p = s.find(k);
    if (p == std::string::npos) return def;
    p += k.size();
    while (p < s.size() && s[p] == ' ') ++p;
    if (p >= s.size()) return def;
    char* end = nullptr;
    double v = strtod(s.c_str() + p, &end);
    return (end == s.c_str() + p) ? def : v;
}

// Extract array of JSON objects for "key":[{...},{...}]
// Also handles single-object case "key":{...}
static std::vector<std::string> jarr(const std::string& s, const std::string& key) {
    std::vector<std::string> res;
    // Try array
    auto k = "\"" + key + "\":[";
    auto p = s.find(k);
    if (p != std::string::npos) {
        p += k.size();
        while (p < s.size()) {
            while (p < s.size() && s[p] != '{' && s[p] != ']') ++p;
            if (p >= s.size() || s[p] == ']') break;
            size_t st = p; int depth = 0;
            for (; p < s.size(); ++p) {
                if (s[p] == '"') {
                    ++p;
                    while (p < s.size() && !(s[p] == '"' && s[p-1] != '\\')) ++p;
                } else if (s[p] == '{') ++depth;
                else if (s[p] == '}') { if (--depth == 0) break; }
            }
            res.push_back(s.substr(st, p - st + 1));
            ++p;
        }
        return res;
    }
    // Try single object
    k = "\"" + key + "\":{";
    p = s.find(k);
    if (p != std::string::npos) {
        p += k.size() - 1;
        size_t st = p; int depth = 0;
        for (; p < s.size(); ++p) {
            if (s[p] == '"') { ++p; while (p < s.size() && !(s[p] == '"' && s[p-1] != '\\')) ++p; }
            else if (s[p] == '{') ++depth;
            else if (s[p] == '}') { if (--depth == 0) break; }
        }
        if (depth == 0) res.push_back(s.substr(st, p - st + 1));
    }
    return res;
}

// Parse one "song"/"entry" JSON object into a Song. Subsonic reports a favorite
// as a "starred" timestamp string, so presence — not value — is the flag.
static navidrome::Song parseSongObj(const std::string& s) {
    navidrome::Song so;
    so.id         = jstr(s, "id");
    so.title      = jstr(s, "title", "Unknown Title");
    so.artist     = jstr(s, "artist");
    so.artistId   = jstr(s, "artistId");
    so.album      = jstr(s, "album");
    so.albumId    = jstr(s, "albumId");
    so.coverArtId = jstr(s, "coverArt");
    so.suffix     = jstr(s, "suffix");
    so.track      = jint(s, "track");
    so.year       = jint(s, "year");
    so.duration   = jdbl(s, "duration");
    so.starred    = !jstr(s, "starred").empty();
    so.rating     = jint(s, "userRating");
    return so;
}

static navidrome::Album parseAlbumObj(const std::string& a) {
    navidrome::Album al;
    al.id         = jstr(a, "id");
    al.name       = jstr(a, "name", "Unknown Album");
    al.artist     = jstr(a, "artist");
    al.artistId   = jstr(a, "artistId");
    al.coverArtId = jstr(a, "coverArt");
    al.year       = jint(a, "year");
    al.songCount  = jint(a, "songCount");
    al.starred    = !jstr(a, "starred").empty();
    return al;
}

// Check Subsonic status and return inner response object, or set error
static std::string checkResponse(const std::string& body, std::string& outError) {
    auto res = jstr(body, "status");
    if (res != "ok") {
        auto arr = jarr(body, "error");
        outError = arr.empty() ? "Unknown Subsonic error" : jstr(arr[0], "message", "Error");
        return "";
    }
    // Return everything inside "subsonic-response":{...}
    std::string k = "\"subsonic-response\":{";
    auto p = body.find(k);
    if (p == std::string::npos) { outError = "Invalid response"; return ""; }
    p += k.size() - 1;
    size_t st = p; int depth = 0;
    for (; p < body.size(); ++p) {
        if (body[p] == '"') { ++p; while (p < body.size() && !(body[p] == '"' && body[p-1] != '\\')) ++p; }
        else if (body[p] == '{') ++depth;
        else if (body[p] == '}') { if (--depth == 0) break; }
    }
    return body.substr(st, p - st + 1);
}

// ---------------------------------------------------------------------------
// SubsonicClientWin
// ---------------------------------------------------------------------------

navidrome::SubsonicClientWin& navidrome::SubsonicClientWin::get() {
    static SubsonicClientWin inst;
    return inst;
}

bool navidrome::SubsonicClientWin::isConfigured() const {
    return cfg_server_url.get().length() > 0 &&
           cfg_username.get().length()   > 0 &&
           cfg_password.get().length()   > 0;
}

navidrome::SubsonicRequestContext navidrome::SubsonicClientWin::snapshot() const {
    SubsonicRequestContext context;
    context.serverUrl = cfg_server_url.get().c_str();
    context.username = cfg_username.get().c_str();
    context.password = cfg_password.get().c_str();
    context.salt = cfg_salt.get().length() > 0 ? cfg_salt.get().c_str() : "fb2k_navidrome";
    context.customHeaders = cfg_custom_headers.get().c_str();
    return context;
}

std::string navidrome::SubsonicClientWin::generateToken(const std::string& password,
                                                         const std::string& salt) {
    return md5hex(password + salt);
}

std::string navidrome::SubsonicClientWin::authParams() const {
    std::string user = cfg_username.get().c_str();
    std::string pass = cfg_password.get().c_str();
    std::string salt = cfg_salt.get().length() > 0 ? cfg_salt.get().c_str() : "fb2k_navidrome";
    std::string token = md5hex(pass + salt);
    return "u=" + urlEncode(user) + "&t=" + token + "&s=" + salt +
           "&v=1.16.1&c=foo_navidrome&f=json";
}

std::string navidrome::SubsonicClientWin::buildURL(const std::string& endpoint,
                                                    const std::string& extra) const {
    std::string base = cfg_server_url.get().c_str();
    while (!base.empty() && base.back() == '/') base.pop_back();
    std::string url = base + "/rest/" + endpoint + "?" + authParams();
    if (!extra.empty()) url += "&" + extra;
    return url;
}

std::vector<std::string> navidrome::SubsonicClientWin::customHeaderLines() {
    return navidrome::parseHeaderLines(cfg_custom_headers.get().c_str());
}

std::wstring navidrome::SubsonicClientWin::customHeadersWide() {
    std::string joined;
    for (const auto& line : customHeaderLines()) {
        if (!joined.empty()) joined += "\r\n";
        joined += line;
    }
    return joined.empty() ? std::wstring() : toWide(joined);
}

std::string navidrome::SubsonicClientWin::httpGet(const std::string& urlStr,
                                                   std::string& outError) const {
    std::wstring wurl = toWide(urlStr);

    URL_COMPONENTS uc = {};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256] = {}, path[4096] = {};
    uc.lpszHostName    = host; uc.dwHostNameLength    = 256;
    uc.lpszUrlPath     = path; uc.dwUrlPathLength     = 4096;

    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) {
        outError = "Invalid URL"; return "";
    }

    HINTERNET hSess = WinHttpOpen(L"foo_navidrome/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSess) { outError = "WinHttpOpen failed"; return ""; }
    WinHttpSetTimeouts(hSess, 0, 15000, 15000, 30000);
    applySecureProtocols(hSess);

    HINTERNET hConn = WinHttpConnect(hSess, host, uc.nPort, 0);
    if (!hConn) { WinHttpCloseHandle(hSess); outError = "Connect failed"; return ""; }

    DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hReq = WinHttpOpenRequest(hConn, L"GET", path,
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);

    std::string result;
    if (hReq) {
        std::wstring hdrs = customHeadersWide();
        if (!hdrs.empty())
            WinHttpAddRequestHeaders(hReq, hdrs.c_str(), (DWORD)-1,
                WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
        if (WinHttpSendRequest(hReq, nullptr, 0, nullptr, 0, 0, 0) &&
            WinHttpReceiveResponse(hReq, nullptr)) {
            DWORD status = 0, sz = sizeof(status);
            WinHttpQueryHeaders(hReq,
                WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                nullptr, &status, &sz, nullptr);
            if (status == 200) {
                DWORD avail = 0;
                while (WinHttpQueryDataAvailable(hReq, &avail) && avail > 0) {
                    std::string chunk(avail, '\0');
                    DWORD read = 0;
                    WinHttpReadData(hReq, &chunk[0], avail, &read);
                    result.append(chunk, 0, read);
                }
            } else {
                outError = "HTTP " + std::to_string(status);
            }
        } else {
            outError = "Request failed (err=" + std::to_string(GetLastError()) + ")";
        }
        WinHttpCloseHandle(hReq);
    }
    WinHttpCloseHandle(hConn);
    WinHttpCloseHandle(hSess);
    return result;
}

bool navidrome::SubsonicClientWin::ping(std::string& outError) {
    std::string body = httpGet(buildURL("ping.view"), outError);
    if (body.empty()) return false;
    auto root = checkResponse(body, outError);
    return !root.empty();
}

std::vector<navidrome::Artist> navidrome::SubsonicClientWin::getArtists(std::string& outError) {
    std::string body = httpGet(buildURL("getArtists.view"), outError);
    if (body.empty()) return {};
    auto root = checkResponse(body, outError);
    if (root.empty()) return {};

    std::vector<Artist> result;
    for (auto& idxObj : jarr(root, "index")) {
        for (auto& a : jarr(idxObj, "artist")) {
            Artist ar;
            ar.id         = jstr(a, "id");
            ar.name       = jstr(a, "name", "Unknown Artist");
            ar.coverArtId = jstr(a, "coverArt");
            ar.albumCount = jint(a, "albumCount");
            result.push_back(std::move(ar));
        }
    }
    return result;
}

std::vector<navidrome::Album>
navidrome::SubsonicClientWin::getAlbumsForArtist(const std::string& artistId,
                                                  std::string& outError) {
    std::string body = httpGet(buildURL("getArtist.view", "id=" + urlEncode(artistId)), outError);
    if (body.empty()) return {};
    auto root = checkResponse(body, outError);
    if (root.empty()) return {};

    std::vector<Album> result;
    for (auto& a : jarr(root, "album")) {
        Album al = parseAlbumObj(a);
        if (al.artistId.empty()) al.artistId = artistId;
        result.push_back(std::move(al));
    }
    return result;
}

std::vector<navidrome::Song>
navidrome::SubsonicClientWin::getSongsForAlbum(const std::string& albumId,
                                                std::string& outError) {
    std::string body = httpGet(buildURL("getAlbum.view", "id=" + urlEncode(albumId)), outError);
    if (body.empty()) return {};
    auto root = checkResponse(body, outError);
    if (root.empty()) return {};

    std::vector<Song> result;
    for (auto& s : jarr(root, "song")) {
        Song so = parseSongObj(s);
        if (so.albumId.empty()) so.albumId = albumId;
        result.push_back(std::move(so));
    }
    return result;
}

navidrome::SearchResults
navidrome::SubsonicClientWin::search(const std::string& query, std::string& outError) {
    std::string params = "query=" + urlEncode(query) +
                         "&artistCount=20&albumCount=20&songCount=50";
    std::string body = httpGet(buildURL("search3.view", params), outError);
    if (body.empty()) return {};
    auto root = checkResponse(body, outError);
    if (root.empty()) return {};

    SearchResults r;
    for (auto& a : jarr(root, "artist")) {
        Artist ar; ar.id = jstr(a,"id"); ar.name = jstr(a,"name"); ar.coverArtId = jstr(a,"coverArt");
        ar.starred = !jstr(a,"starred").empty();
        r.artists.push_back(ar);
    }
    for (auto& a : jarr(root, "album"))
        r.albums.push_back(parseAlbumObj(a));
    for (auto& s : jarr(root, "song"))
        r.songs.push_back(parseSongObj(s));
    return r;
}

// ---------------------------------------------------------------------------
// Smart lists, favorites, ratings, playlists, scrobbling
// ---------------------------------------------------------------------------

std::vector<navidrome::Album>
navidrome::SubsonicClientWin::getAlbumList(AlbumListType type, int size,
                                            std::string& outError) {
    std::string params = std::string("type=") + albumListTypeName(type) +
                         "&size=" + std::to_string(size);
    std::string body = httpGet(buildURL("getAlbumList2.view", params), outError);
    if (body.empty()) return {};
    auto root = checkResponse(body, outError);
    if (root.empty()) return {};

    std::vector<Album> result;
    for (auto& a : jarr(root, "album"))
        result.push_back(parseAlbumObj(a));
    return result;
}

std::vector<navidrome::Song>
navidrome::SubsonicClientWin::getStarredSongs(std::string& outError) {
    std::string body = httpGet(buildURL("getStarred2.view"), outError);
    if (body.empty()) return {};
    auto root = checkResponse(body, outError);
    if (root.empty()) return {};

    std::vector<Song> result;
    for (auto& s : jarr(root, "song")) {
        Song so = parseSongObj(s);
        so.starred = true;   // getStarred2 omits the per-item "starred" field
        result.push_back(std::move(so));
    }
    return result;
}

std::vector<navidrome::Genre>
navidrome::SubsonicClientWin::getGenres(std::string& outError) {
    std::string body = httpGet(buildURL("getGenres.view"), outError);
    if (body.empty()) return {};
    auto root = checkResponse(body, outError);
    if (root.empty()) return {};

    std::vector<Genre> result;
    for (auto& g : jarr(root, "genre")) {
        Genre gen;
        // Subsonic puts the genre name in "value"; skip the empty "no genre"
        // bucket some servers report.
        gen.name = jstr(g, "value");
        if (gen.name.empty()) continue;
        gen.songCount  = jint(g, "songCount");
        gen.albumCount = jint(g, "albumCount");
        result.push_back(std::move(gen));
    }
    return result;
}

std::vector<navidrome::Song>
navidrome::SubsonicClientWin::getSongsForGenre(const std::string& genre, int count,
                                                std::string& outError) {
    if (genre.empty()) return {};
    std::string params = "genre=" + urlEncode(genre) + "&count=" + std::to_string(count);
    std::string body = httpGet(buildURL("getSongsByGenre.view", params), outError);
    if (body.empty()) return {};
    auto root = checkResponse(body, outError);
    if (root.empty()) return {};

    std::vector<Song> result;
    for (auto& s : jarr(root, "song"))
        result.push_back(parseSongObj(s));
    return result;
}

bool navidrome::SubsonicClientWin::setStarred(bool starred, const std::string& itemId,
                                               StarKind kind, std::string& outError) {
    if (itemId.empty()) return false;
    std::string params = std::string(starParamName(kind)) + "=" + urlEncode(itemId);
    std::string body = httpGet(buildURL(starred ? "star.view" : "unstar.view", params),
                               outError);
    if (body.empty()) return false;
    return !checkResponse(body, outError).empty();
}

bool navidrome::SubsonicClientWin::setRating(int rating, const std::string& songId,
                                              std::string& outError) {
    if (songId.empty()) return false;
    if (rating < 0) rating = 0;
    if (rating > 5) rating = 5;
    std::string params = "id=" + urlEncode(songId) + "&rating=" + std::to_string(rating);
    std::string body = httpGet(buildURL("setRating.view", params), outError);
    if (body.empty()) return false;
    return !checkResponse(body, outError).empty();
}

std::vector<navidrome::Playlist>
navidrome::SubsonicClientWin::getPlaylists(std::string& outError) {
    std::string body = httpGet(buildURL("getPlaylists.view"), outError);
    if (body.empty()) return {};
    auto root = checkResponse(body, outError);
    if (root.empty()) return {};

    std::vector<Playlist> result;
    for (auto& p : jarr(root, "playlist")) {
        Playlist pl;
        pl.id        = jstr(p, "id");
        pl.name      = jstr(p, "name", "Unnamed playlist");
        pl.owner     = jstr(p, "owner");
        pl.songCount = jint(p, "songCount");
        pl.duration  = jdbl(p, "duration");
        result.push_back(std::move(pl));
    }
    return result;
}

std::vector<navidrome::Song>
navidrome::SubsonicClientWin::getPlaylistSongs(const std::string& playlistId,
                                                std::string& outError) {
    std::string body = httpGet(buildURL("getPlaylist.view", "id=" + urlEncode(playlistId)),
                               outError);
    if (body.empty()) return {};
    auto root = checkResponse(body, outError);
    if (root.empty()) return {};

    std::vector<Song> result;
    for (auto& s : jarr(root, "entry"))
        result.push_back(parseSongObj(s));
    return result;
}

// Subsonic passes track ids on the query string, and both our WinHTTP path
// buffer (4096 wchars) and typical server URL limits cap how many fit in one
// request — so the playlist is created with the first chunk and grown with
// updatePlaylist.view calls.
std::string navidrome::SubsonicClientWin::createPlaylist(
        const std::string& name, const std::vector<std::string>& songIds,
        std::string& outError) {
    if (name.empty()) return "";
    constexpr std::size_t kChunk = kPlaylistChunkSize;

    std::size_t first = (std::min)(kChunk, songIds.size());
    std::string params = "name=" + urlEncode(name);
    for (std::size_t i = 0; i < first; ++i) params += "&songId=" + urlEncode(songIds[i]);

    std::string body = httpGet(buildURL("createPlaylist.view", params), outError);
    if (body.empty()) return "";
    auto root = checkResponse(body, outError);
    if (root.empty()) return "";

    // Navidrome echoes the created playlist back; without its id the remaining
    // tracks can't be appended (and the caller can't act on the new playlist).
    std::string playlistId;
    auto created = jarr(root, "playlist");
    if (!created.empty()) playlistId = jstr(created[0], "id");

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

bool navidrome::SubsonicClientWin::addToPlaylist(const std::string& playlistId,
                                                  const std::vector<std::string>& songIds,
                                                  std::string& outError) {
    if (playlistId.empty() || songIds.empty()) return false;
    constexpr std::size_t kChunk = kPlaylistChunkSize;

    for (std::size_t i = 0; i < songIds.size(); i += kChunk) {
        std::string upd = "playlistId=" + urlEncode(playlistId);
        for (std::size_t j = i; j < (std::min)(i + kChunk, songIds.size()); ++j)
            upd += "&songIdToAdd=" + urlEncode(songIds[j]);
        std::string body = httpGet(buildURL("updatePlaylist.view", upd), outError);
        if (body.empty() || checkResponse(body, outError).empty()) return false;
    }
    return true;
}

// songIndexToRemove refers to a track's position in the playlist as it stands
// when the request is served, so removals are sent highest-index-first: dropping
// a later entry never shifts an earlier one.
bool navidrome::SubsonicClientWin::removeFromPlaylist(const std::string& playlistId,
                                                       const std::vector<int>& indexes,
                                                       std::string& outError) {
    if (playlistId.empty() || indexes.empty()) return false;
    constexpr std::size_t kChunk = kPlaylistChunkSize;

    std::vector<int> sorted = indexes;
    std::sort(sorted.begin(), sorted.end(), std::greater<int>());

    for (std::size_t i = 0; i < sorted.size(); i += kChunk) {
        std::string upd = "playlistId=" + urlEncode(playlistId);
        for (std::size_t j = i; j < (std::min)(i + kChunk, sorted.size()); ++j)
            upd += "&songIndexToRemove=" + std::to_string(sorted[j]);
        std::string body = httpGet(buildURL("updatePlaylist.view", upd), outError);
        if (body.empty() || checkResponse(body, outError).empty()) return false;
    }
    return true;
}

bool navidrome::SubsonicClientWin::renamePlaylist(const std::string& playlistId,
                                                   const std::string& name,
                                                   std::string& outError) {
    if (playlistId.empty() || name.empty()) return false;
    std::string params = "playlistId=" + urlEncode(playlistId) + "&name=" + urlEncode(name);
    std::string body = httpGet(buildURL("updatePlaylist.view", params), outError);
    if (body.empty()) return false;
    return !checkResponse(body, outError).empty();
}

bool navidrome::SubsonicClientWin::deletePlaylist(const std::string& playlistId,
                                                   std::string& outError) {
    if (playlistId.empty()) return false;
    std::string body = httpGet(buildURL("deletePlaylist.view", "id=" + urlEncode(playlistId)),
                               outError);
    if (body.empty()) return false;
    return !checkResponse(body, outError).empty();
}

std::vector<navidrome::Bookmark>
navidrome::SubsonicClientWin::getBookmarks(std::string& outError) {
    std::string body = httpGet(buildURL("getBookmarks.view"), outError);
    if (body.empty()) return {};
    auto root = checkResponse(body, outError);
    if (root.empty()) return {};

    std::vector<Bookmark> result;
    for (auto& b : jarr(root, "bookmark")) {
        auto entries = jarr(b, "entry");
        if (entries.empty()) continue;
        Bookmark bm;
        bm.song       = parseSongObj(entries[0]);
        bm.positionMs = jdbl(b, "position");
        bm.comment    = jstr(b, "comment");
        result.push_back(std::move(bm));
    }
    return result;
}

bool navidrome::SubsonicClientWin::createBookmark(const std::string& songId, double positionMs,
                                                   const std::string& comment,
                                                   std::string& outError) {
    if (songId.empty()) return false;
    std::string params = "id=" + urlEncode(songId) +
                         "&position=" + std::to_string(static_cast<long long>(positionMs));
    if (!comment.empty()) params += "&comment=" + urlEncode(comment);
    std::string body = httpGet(buildURL("createBookmark.view", params), outError);
    if (body.empty()) return false;
    return !checkResponse(body, outError).empty();
}

bool navidrome::SubsonicClientWin::deleteBookmark(const std::string& songId,
                                                   std::string& outError) {
    if (songId.empty()) return false;
    std::string body = httpGet(buildURL("deleteBookmark.view", "id=" + urlEncode(songId)),
                               outError);
    if (body.empty()) return false;
    return !checkResponse(body, outError).empty();
}

bool navidrome::SubsonicClientWin::scrobble(const std::string& songId, bool submission,
                                             std::string& outError) {
    if (songId.empty()) return false;
    std::string params = "id=" + urlEncode(songId) +
                         "&submission=" + (submission ? "true" : "false");
    std::string body = httpGet(buildURL("scrobble.view", params), outError);
    if (body.empty()) return false;
    return !checkResponse(body, outError).empty();
}

std::string navidrome::SubsonicClientWin::streamURL(const std::string& songId) {
    // Transcoding preferences — the server falls back to its own defaults when
    // neither is set.
    std::string extra = "id=" + urlEncode(songId) +
        streamTranscodeParams(cfg_stream_format.get().c_str(),
                              static_cast<int>(cfg_max_bitrate.get()));
    return buildURL("stream.view", extra);
}

std::string navidrome::SubsonicClientWin::downloadURL(const std::string& songId) {
    return buildURL("download.view", "id=" + urlEncode(songId));
}

std::string navidrome::SubsonicClientWin::coverArtURL(const std::string& id, int size) {
    std::string extra = "id=" + urlEncode(id);
    if (size > 0) extra += "&size=" + std::to_string(size);
    return buildURL("getCoverArt.view", extra);
}

std::string navidrome::SubsonicClientWin::coverArtURL(
        const SubsonicRequestContext& context, const std::string& id, int size) const {
    return buildCoverArtUrl(context.serverUrl, context.username, context.password,
        context.salt, id, size);
}

// ---------------------------------------------------------------------------
// Streaming download to disk — separate from both httpGet() (which builds the
// body into a std::string) and httpGetBinary() (which caps the size and sniffs
// for image content). A full-quality track is neither text nor small.
// ---------------------------------------------------------------------------
bool navidrome::SubsonicClientWin::httpDownloadToFile(const std::string& urlStr,
                                                       const std::wstring& destPath,
                                                       std::string& outError) const {
    std::wstring wurl = toWide(urlStr);

    URL_COMPONENTS uc = {};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256] = {}, path[4096] = {};
    uc.lpszHostName = host; uc.dwHostNameLength = 256;
    uc.lpszUrlPath  = path; uc.dwUrlPathLength  = 4096;

    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) { outError = "Invalid URL"; return false; }

    HINTERNET hSess = WinHttpOpen(L"foo_navidrome/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSess) { outError = "WinHttpOpen failed"; return false; }
    // A track download can run far longer than an API call.
    WinHttpSetTimeouts(hSess, 0, 15000, 15000, 300000);
    applySecureProtocols(hSess);

    HINTERNET hConn = WinHttpConnect(hSess, host, uc.nPort, 0);
    if (!hConn) {
        WinHttpCloseHandle(hSess);
        outError = "Connect failed";
        return false;
    }

    DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hReq = WinHttpOpenRequest(hConn, L"GET", path,
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hReq) {
        WinHttpCloseHandle(hConn);
        WinHttpCloseHandle(hSess);
        outError = "WinHttpOpenRequest failed";
        return false;
    }

    std::wstring hdrs = customHeadersWide();
    if (!hdrs.empty())
        WinHttpAddRequestHeaders(hReq, hdrs.c_str(), (DWORD)-1,
            WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);

    bool ok = false;
    if (WinHttpSendRequest(hReq, nullptr, 0, nullptr, 0, 0, 0) &&
        WinHttpReceiveResponse(hReq, nullptr)) {
        DWORD status = 0, sz = sizeof(status);
        WinHttpQueryHeaders(hReq,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            nullptr, &status, &sz, nullptr);
        if (status != 200) {
            outError = "HTTP " + std::to_string(status);
        } else {
            HANDLE hFile = CreateFileW(destPath.c_str(), GENERIC_WRITE, 0, nullptr,
                                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hFile == INVALID_HANDLE_VALUE) {
                outError = "Cannot create file (err=" +
                           std::to_string(GetLastError()) + ")";
            } else {
                ok = true;
                DWORD avail = 0;
                while (ok && WinHttpQueryDataAvailable(hReq, &avail) && avail > 0) {
                    std::vector<char> chunk(avail);
                    DWORD read = 0;
                    if (!WinHttpReadData(hReq, chunk.data(), avail, &read)) {
                        outError = "Read failed (err=" +
                                   std::to_string(GetLastError()) + ")";
                        ok = false;
                        break;
                    }
                    DWORD written = 0;
                    if (!WriteFile(hFile, chunk.data(), read, &written, nullptr) ||
                        written != read) {
                        outError = "Write failed (err=" +
                                   std::to_string(GetLastError()) + ")";
                        ok = false;
                        break;
                    }
                }
                CloseHandle(hFile);
                // Don't leave a truncated file behind on a mid-stream failure.
                if (!ok) DeleteFileW(destPath.c_str());
            }
        }
    } else {
        outError = "Request failed (err=" + std::to_string(GetLastError()) + ")";
    }

    WinHttpCloseHandle(hReq);
    WinHttpCloseHandle(hConn);
    WinHttpCloseHandle(hSess);
    return ok;
}

// ---------------------------------------------------------------------------
// Binary fetch for cover art — separate from httpGet() because it needs raw
// bytes (not text), a size cap, Content-Type sniffing and abort_callback
// cooperation so a background art fetch can be cancelled mid-read.
// ---------------------------------------------------------------------------
navidrome::SubsonicClientWin::BinaryFetchResult
navidrome::SubsonicClientWin::httpGetBinary(
        const SubsonicRequestContext& context,
        const std::string& urlStr,
        std::size_t maxBytes,
        abort_callback& abort) const {

    BinaryFetchResult result;
    result.cls = FetchClass::Transport;
    result.httpStatus = 0;

    std::wstring wurl = toWide(urlStr);

    URL_COMPONENTS uc = {};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256] = {}, path[4096] = {};
    uc.lpszHostName = host; uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path; uc.dwUrlPathLength = 4096;

    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) {
        return result; // Transport
    }

    HINTERNET hSess = WinHttpOpen(L"foo_navidrome/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSess) return result;

    WinHttpSetTimeouts(hSess, 0, 15000, 15000, 30000);
    applySecureProtocols(hSess);

    // Check abort before connect
    if (abort.is_aborting()) {
        WinHttpCloseHandle(hSess);
        result.cls = FetchClass::Aborted;
        return result;
    }

    HINTERNET hConn = WinHttpConnect(hSess, host, uc.nPort, 0);
    if (!hConn) {
        WinHttpCloseHandle(hSess);
        return result;
    }

    DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hReq = WinHttpOpenRequest(hConn, L"GET", path,
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);

    if (!hReq) {
        WinHttpCloseHandle(hConn);
        WinHttpCloseHandle(hSess);
        return result;
    }

    // Apply custom headers from the given context (not the live cfg globals)
    std::string joined;
    for (const auto& line : navidrome::parseHeaderLines(context.customHeaders)) {
        if (!joined.empty()) joined += "\r\n";
        joined += line;
    }
    std::wstring hdrs = joined.empty() ? std::wstring() : toWide(joined);
    if (!hdrs.empty()) {
        WinHttpAddRequestHeaders(hReq, hdrs.c_str(), (DWORD)-1,
            WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
    }

    // Check abort before send
    if (abort.is_aborting()) {
        WinHttpCloseHandle(hReq);
        WinHttpCloseHandle(hConn);
        WinHttpCloseHandle(hSess);
        result.cls = FetchClass::Aborted;
        return result;
    }

    if (!WinHttpSendRequest(hReq, nullptr, 0, nullptr, 0, 0, 0) ||
        !WinHttpReceiveResponse(hReq, nullptr)) {
        WinHttpCloseHandle(hReq);
        WinHttpCloseHandle(hConn);
        WinHttpCloseHandle(hSess);
        return result; // Transport
    }

    // Query status
    DWORD status = 0, sz = sizeof(status);
    if (!WinHttpQueryHeaders(hReq,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        nullptr, &status, &sz, nullptr)) {
        status = 0;
    }
    result.httpStatus = status;
    result.cls = classifyHttpStatus(status);

    // Query Content-Type
    wchar_t ctBuf[256] = {};
    DWORD ctLen = sizeof(ctBuf);
    if (WinHttpQueryHeaders(hReq, WINHTTP_QUERY_CONTENT_TYPE,
        nullptr, ctBuf, &ctLen, nullptr)) {
        result.contentType = toUtf8(ctBuf);
    }

    // Read body (only for 200)
    if (status == 200) {
        std::vector<uint8_t> body;
        bool readSucceeded = true;
        for (;;) {
            DWORD avail = 0;
            if (!WinHttpQueryDataAvailable(hReq, &avail)) {
                readSucceeded = false;
                break;
            }
            if (avail == 0) break;

            // Check abort between chunks
            if (abort.is_aborting()) {
                WinHttpCloseHandle(hReq);
                WinHttpCloseHandle(hConn);
                WinHttpCloseHandle(hSess);
                result.cls = FetchClass::Aborted;
                return result;
            }

            // Check size limit
            if (body.size() > maxBytes || avail > maxBytes - body.size()) {
                WinHttpCloseHandle(hReq);
                WinHttpCloseHandle(hConn);
                WinHttpCloseHandle(hSess);
                result.cls = FetchClass::InvalidContent;
                return result;
            }

            std::vector<uint8_t> chunk(avail);
            DWORD read = 0;
            if (!WinHttpReadData(hReq, chunk.data(), avail, &read)) {
                readSucceeded = false;
                break;
            }
            body.insert(body.end(), chunk.begin(), chunk.begin() + read);
        }

        if (abort.is_aborting()) {
            result.cls = FetchClass::Aborted;
        } else if (!readSucceeded) {
            result.cls = FetchClass::Transport;
        } else {
            result.cls = classifyBody(result.contentType, body, maxBytes);
            if (result.cls == FetchClass::Ok) result.body = std::move(body);
        }
    }

    WinHttpCloseHandle(hReq);
    WinHttpCloseHandle(hConn);
    WinHttpCloseHandle(hSess);
    return result;
}
