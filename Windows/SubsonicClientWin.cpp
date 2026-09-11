#include "stdafx.h"
#include "SubsonicClientWin.h"
#include "MediaEnrichmentLogic.h"
#include "../NavidromeDebugLog.h"
#include <SDK/cfg_var.h>

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
    extern cfg_var_modern::cfg_bool cfg_library_filter;
    extern cfg_string cfg_library_ids;
}

// ---------------------------------------------------------------------------
// The request bodies (URL assembly, retry loop, status-wrapper check, json
// walk, multi-library fan-out) all live in navidrome::SubsonicCore now. This
// file is the Windows adapter: a WinHTTP-backed IHttpTransport, a cfg_*-backed
// ISettingsProvider, plus the binary cover-art fetch and streaming download
// which never went through the JSON path.
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

// Map a WinHTTP GetLastError() value to the shared ErrorKind so SubsonicCore's
// retry loop can tell a transient socket failure from a dead-certain one.
static navidrome::ErrorKind classifyWinHttpError(DWORD err) {
    switch (err) {
        case 12002: // ERROR_WINHTTP_TIMEOUT
            return navidrome::ErrorKind::Timeout;
        case 12007: // ERROR_WINHTTP_NAME_NOT_RESOLVED
        case 12029: // ERROR_WINHTTP_CANNOT_CONNECT
        case 12030: // ERROR_WINHTTP_CONNECTION_ERROR
        case 12152: // ERROR_WINHTTP_INVALID_SERVER_RESPONSE
            return navidrome::ErrorKind::Network;
        case 12157: // ERROR_WINHTTP_SECURE_CHANNEL_ERROR
        case 12175: // ERROR_WINHTTP_SECURE_FAILURE
            return navidrome::ErrorKind::Tls;
        default:
            return navidrome::ErrorKind::Network;
    }
}

// The server rejecting the configured credentials is a deterministic, user-
// actionable state — say so once per session in the console (every subsequent
// call would just repeat it). Cheap racy flag: worst case is two prints.
static void warnAuthOnce() {
    static bool warned = false;
    if (warned) return;
    warned = true;
    console::print("Navidrome: the server rejected the configured credentials — "
                   "check Preferences \xE2\x80\xBA Tools \xE2\x80\xBA Navidrome");
}

// RAII for a WinHTTP handle so an early return on any error path still closes
// it — the old hand-rolled close chain leaked hReq whenever an error branch
// returned before reaching its WinHttpCloseHandle.
namespace {
struct WinHttpHandle {
    HINTERNET h = nullptr;
    WinHttpHandle() = default;
    explicit WinHttpHandle(HINTERNET handle) : h(handle) {}
    ~WinHttpHandle() { if (h) WinHttpCloseHandle(h); }
    WinHttpHandle(const WinHttpHandle&) = delete;
    WinHttpHandle& operator=(const WinHttpHandle&) = delete;
    WinHttpHandle(WinHttpHandle&& o) noexcept : h(o.h) { o.h = nullptr; }
    WinHttpHandle& operator=(WinHttpHandle&& o) noexcept {
        if (this != &o) { if (h) WinHttpCloseHandle(h); h = o.h; o.h = nullptr; }
        return *this;
    }
    operator HINTERNET() const { return h; }
    explicit operator bool() const { return h != nullptr; }
};
} // namespace

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

// ---------------------------------------------------------------------------
// The IHttpTransport + ISettingsProvider SubsonicCore runs on.
// ---------------------------------------------------------------------------
namespace {

// One synchronous WinHTTP GET — no retry (SubsonicCore drives that), no status
// wrapper parsing. Fills HttpResult::body on a clean 200, otherwise classifies
// the failure into HttpResult::error.
struct WinHttpTransport : navidrome::IHttpTransport {
    navidrome::HttpResult getOnce(const std::string& urlStr) override {
        using navidrome::ErrorKind;
        navidrome::HttpResult out;

        std::wstring wurl = toWide(urlStr);
        URL_COMPONENTS uc = {};
        uc.dwStructSize = sizeof(uc);
        wchar_t host[256] = {}, path[4096] = {};
        uc.lpszHostName = host; uc.dwHostNameLength = 256;
        uc.lpszUrlPath  = path; uc.dwUrlPathLength  = 4096;
        if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) {
            out.error = { ErrorKind::Parse, 0, 0, "Invalid URL" };
            return out;
        }

        WinHttpHandle sess(WinHttpOpen(L"foo_navidrome/1.0",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
        if (!sess) {
            out.error = { ErrorKind::Network, 0, 0, "WinHttpOpen failed" };
            return out;
        }
        WinHttpSetTimeouts(sess, 0, 15000, 15000, 30000);
        applySecureProtocols(sess);

        WinHttpHandle conn(WinHttpConnect(sess, host, uc.nPort, 0));
        if (!conn) {
            out.error = { classifyWinHttpError(GetLastError()), 0, 0, "Connect failed" };
            return out;
        }
        const DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
        const std::wstring hdrs = navidrome::SubsonicClientWin::customHeadersWide();

        WinHttpHandle req(WinHttpOpenRequest(conn, L"GET", path, nullptr,
            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
        if (!req) {
            out.error = { ErrorKind::Network, 0, 0,
                "WinHttpOpenRequest failed (err=" + std::to_string(GetLastError()) + ")" };
            return out;
        }
        if (!hdrs.empty())
            WinHttpAddRequestHeaders(req, hdrs.c_str(), (DWORD)-1,
                WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
        if (!WinHttpSendRequest(req, nullptr, 0, nullptr, 0, 0, 0) ||
            !WinHttpReceiveResponse(req, nullptr)) {
            DWORD e = GetLastError();
            ErrorKind kind = classifyWinHttpError(e);
            out.error = { kind, 0, 0, std::string(navidrome::errorKindName(kind)) +
                          " (winhttp err=" + std::to_string(e) + ")" };
            return out;
        }
        DWORD status = 0, sz = sizeof(status);
        WinHttpQueryHeaders(req,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            nullptr, &status, &sz, nullptr);
        ErrorKind kind = navidrome::httpStatusToErrorKind((int)status);
        if (kind != ErrorKind::None) {
            out.error = { kind, (int)status, 0, "HTTP " + std::to_string(status) };
            return out;
        }
        DWORD avail = 0;
        while (WinHttpQueryDataAvailable(req, &avail) && avail > 0) {
            std::string chunk(avail, '\0');
            DWORD read = 0;
            WinHttpReadData(req, &chunk[0], avail, &read);
            out.body.append(chunk, 0, read);
        }
        out.error = { ErrorKind::None, (int)status, 0, {} };
        return out;
    }

    void onAuthRejected() override { warnAuthOnce(); }
};

struct WinSettingsProvider : navidrome::ISettingsProvider {
    navidrome::SubsonicSettings load() const override {
        navidrome::SubsonicSettings s;
        s.serverUrl     = navidrome::cfg_server_url.get().c_str();
        s.username      = navidrome::cfg_username.get().c_str();
        s.password      = navidrome::cfg_password.get().c_str();
        s.salt          = navidrome::cfg_salt.get().length() > 0
                              ? navidrome::cfg_salt.get().c_str() : "fb2k_navidrome";
        s.streamFormat  = navidrome::cfg_stream_format.get().c_str();
        s.maxBitrate    = static_cast<int>(navidrome::cfg_max_bitrate.get());
        s.libraryFilter = navidrome::cfg_library_filter.get();
        s.libraryIdsCsv = navidrome::cfg_library_ids.get().c_str();
        return s;
    }
};

} // namespace

// ---------------------------------------------------------------------------
// SubsonicClientWin
// ---------------------------------------------------------------------------
navidrome::SubsonicClientWin& navidrome::SubsonicClientWin::get() {
    static SubsonicClientWin inst;
    return inst;
}

navidrome::SubsonicClientWin::SubsonicClientWin()
    : m_transport(std::make_unique<WinHttpTransport>()),
      m_settingsProvider(std::make_unique<WinSettingsProvider>()),
      m_core(std::make_unique<SubsonicCore>(*m_transport, *m_settingsProvider)) {}

navidrome::SubsonicClientWin::~SubsonicClientWin() = default;

bool navidrome::SubsonicClientWin::isConfigured() const { return m_core->isConfigured(); }

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

// ---------------------------------------------------------------------------
// API surface — every call forwards to the shared core.
// ---------------------------------------------------------------------------
bool navidrome::SubsonicClientWin::ping(std::string& outError) { return m_core->ping(outError); }

std::vector<navidrome::MusicFolder>
navidrome::SubsonicClientWin::getMusicFolders(std::string& outError) {
    return m_core->getMusicFolders(outError);
}
std::vector<navidrome::MusicFolder> navidrome::SubsonicClientWin::cachedMusicFolders() {
    return m_core->cachedMusicFolders();
}
void navidrome::SubsonicClientWin::refreshMusicFolders() { m_core->refreshMusicFolders(); }
std::vector<std::string> navidrome::SubsonicClientWin::activeMusicFolderIds() {
    return m_core->activeMusicFolderIds();
}
std::vector<std::string> navidrome::SubsonicClientWin::libraryGroupingIds() {
    return m_core->libraryGroupingIds();
}

std::vector<navidrome::Artist> navidrome::SubsonicClientWin::getArtists(std::string& outError) {
    return m_core->getArtists(outError);
}
std::vector<navidrome::Artist>
navidrome::SubsonicClientWin::getArtistsForLibrary(const std::string& libraryId,
                                                    std::string& outError) {
    return m_core->getArtistsForLibrary(libraryId, outError);
}
std::vector<navidrome::Album>
navidrome::SubsonicClientWin::getAlbumsForArtist(const std::string& artistId,
                                                  std::string& outError,
                                                  const std::string& scopeLibraryId) {
    return m_core->getAlbumsForArtist(artistId, outError, scopeLibraryId);
}
std::vector<navidrome::Song>
navidrome::SubsonicClientWin::getSongsForAlbum(const std::string& albumId, std::string& outError) {
    return m_core->getSongsForAlbum(albumId, outError);
}
navidrome::SearchResults
navidrome::SubsonicClientWin::search(const std::string& query, std::string& outError) {
    return m_core->search(query, outError);
}

std::vector<navidrome::Album>
navidrome::SubsonicClientWin::getAlbumList(AlbumListType type, int size, std::string& outError) {
    return m_core->getAlbumList(type, size, outError);
}
std::vector<navidrome::Song>
navidrome::SubsonicClientWin::getStarredSongs(std::string& outError) {
    return m_core->getStarredSongs(outError);
}
std::vector<navidrome::Genre> navidrome::SubsonicClientWin::getGenres(std::string& outError) {
    return m_core->getGenres(outError);
}
std::vector<navidrome::Song>
navidrome::SubsonicClientWin::getSongsForGenre(const std::string& genre, int count,
                                                std::string& outError) {
    return m_core->getSongsForGenre(genre, count, outError);
}
std::vector<navidrome::Song>
navidrome::SubsonicClientWin::getSimilarSongs(const std::string& itemId, int count,
                                               std::string& outError) {
    return m_core->getSimilarSongs(itemId, count, outError);
}
std::vector<navidrome::Song>
navidrome::SubsonicClientWin::getRandomSongs(int count, std::string& outError) {
    return m_core->getRandomSongs(count, outError);
}

bool navidrome::SubsonicClientWin::setStarred(bool starred, const std::string& itemId,
                                               StarKind kind, std::string& outError) {
    return m_core->setStarred(starred, itemId, kind, outError);
}
bool navidrome::SubsonicClientWin::setRating(int rating, const std::string& songId,
                                              std::string& outError) {
    return m_core->setRating(rating, songId, outError);
}
bool navidrome::SubsonicClientWin::getSong(const std::string& songId, Song& out,
                                            std::string& outError) {
    return m_core->getSong(songId, out, outError);
}

std::vector<navidrome::Playlist>
navidrome::SubsonicClientWin::getPlaylists(std::string& outError) {
    return m_core->getPlaylists(outError);
}
std::vector<navidrome::Song>
navidrome::SubsonicClientWin::getPlaylistSongs(const std::string& playlistId,
                                                std::string& outError) {
    return m_core->getPlaylistSongs(playlistId, outError);
}
std::string navidrome::SubsonicClientWin::createPlaylist(
        const std::string& name, const std::vector<std::string>& songIds,
        std::string& outError) {
    return m_core->createPlaylist(name, songIds, outError);
}
bool navidrome::SubsonicClientWin::addToPlaylist(const std::string& playlistId,
                                                  const std::vector<std::string>& songIds,
                                                  std::string& outError) {
    return m_core->addToPlaylist(playlistId, songIds, outError);
}
bool navidrome::SubsonicClientWin::removeFromPlaylist(const std::string& playlistId,
                                                       const std::vector<int>& indexes,
                                                       std::string& outError) {
    return m_core->removeFromPlaylist(playlistId, indexes, outError);
}
bool navidrome::SubsonicClientWin::renamePlaylist(const std::string& playlistId,
                                                   const std::string& name,
                                                   std::string& outError) {
    return m_core->renamePlaylist(playlistId, name, outError);
}
bool navidrome::SubsonicClientWin::deletePlaylist(const std::string& playlistId,
                                                   std::string& outError) {
    return m_core->deletePlaylist(playlistId, outError);
}

std::vector<navidrome::RadioStation>
navidrome::SubsonicClientWin::getRadioStations(std::string& outError) {
    return m_core->getRadioStations(outError);
}
std::string navidrome::SubsonicClientWin::createRadioStation(
        const std::string& streamUrl, const std::string& name,
        const std::string& homePageUrl, std::string& outError) {
    return m_core->createRadioStation(streamUrl, name, homePageUrl, outError);
}
bool navidrome::SubsonicClientWin::updateRadioStation(
        const std::string& id, const std::string& streamUrl, const std::string& name,
        const std::string& homePageUrl, std::string& outError) {
    return m_core->updateRadioStation(id, streamUrl, name, homePageUrl, outError);
}
bool navidrome::SubsonicClientWin::deleteRadioStation(const std::string& id,
                                                       std::string& outError) {
    return m_core->deleteRadioStation(id, outError);
}

std::vector<navidrome::Bookmark>
navidrome::SubsonicClientWin::getBookmarks(std::string& outError) {
    return m_core->getBookmarks(outError);
}
bool navidrome::SubsonicClientWin::createBookmark(const std::string& songId, double positionMs,
                                                   const std::string& comment,
                                                   std::string& outError) {
    return m_core->createBookmark(songId, positionMs, comment, outError);
}
bool navidrome::SubsonicClientWin::deleteBookmark(const std::string& songId,
                                                   std::string& outError) {
    return m_core->deleteBookmark(songId, outError);
}

navidrome::ScanStatus navidrome::SubsonicClientWin::startScan(std::string& outError) {
    return m_core->startScan(outError);
}
navidrome::ScanStatus navidrome::SubsonicClientWin::getScanStatus(std::string& outError) {
    return m_core->getScanStatus(outError);
}

bool navidrome::SubsonicClientWin::scrobble(const std::string& songId, bool submission,
                                             std::string& outError) {
    return m_core->scrobble(songId, submission, outError);
}

std::string navidrome::SubsonicClientWin::streamURL(const std::string& songId) {
    return m_core->streamURL(songId);
}
std::string navidrome::SubsonicClientWin::downloadURL(const std::string& songId) {
    return m_core->downloadURL(songId);
}
std::string navidrome::SubsonicClientWin::coverArtURL(const std::string& id, int size) {
    return m_core->coverArtURL(id, size);
}
std::string navidrome::SubsonicClientWin::coverArtURL(
        const SubsonicRequestContext& context, const std::string& id, int size) const {
    return buildCoverArtUrl(context.serverUrl, context.username, context.password,
        context.salt, id, size);
}

// ---------------------------------------------------------------------------
// Streaming download to disk — separate from the core's JSON GET (it builds the
// body into a std::string) and from httpGetBinary() (which caps the size and
// sniffs for image content). A full-quality track is neither text nor small.
// ---------------------------------------------------------------------------
bool navidrome::SubsonicClientWin::httpDownloadToFile(const std::string& urlStr,
                                                       const std::wstring& destPath,
                                                       std::string& outError) const {
    const std::string safeUrl = navidrome::dbg::scrubAuth(urlStr);
    NAVIDROME_TIMER("HTTP", "download " + safeUrl);
    std::wstring wurl = toWide(urlStr);

    URL_COMPONENTS uc = {};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256] = {}, path[4096] = {};
    uc.lpszHostName = host; uc.dwHostNameLength = 256;
    uc.lpszUrlPath  = path; uc.dwUrlPathLength  = 4096;

    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) { outError = "Invalid URL"; return false; }

    WinHttpHandle sess(WinHttpOpen(L"foo_navidrome/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!sess) { outError = "WinHttpOpen failed"; return false; }
    // A track download can run far longer than an API call.
    WinHttpSetTimeouts(sess, 0, 15000, 15000, 300000);
    applySecureProtocols(sess);

    WinHttpHandle conn(WinHttpConnect(sess, host, uc.nPort, 0));
    if (!conn) {
        outError = "Connect failed";
        NAVIDROME_ERR("HTTP", "download connect failed: " + safeUrl);
        return false;
    }

    DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    WinHttpHandle req(WinHttpOpenRequest(conn, L"GET", path,
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
    if (!req) {
        outError = "WinHttpOpenRequest failed";
        return false;
    }
    HINTERNET hReq = req;   // the rest of this function still reads `hReq`

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

    if (ok)
        NAVIDROME_LOG("HTTP", "download ok -> " + toUtf8(destPath));
    else
        NAVIDROME_ERR("HTTP", "download failed (" + outError + "): " + safeUrl);
    return ok;
}

// ---------------------------------------------------------------------------
// Binary fetch for cover art — separate from the core's JSON GET because it
// needs raw bytes (not text), a size cap, Content-Type sniffing and
// abort_callback cooperation so a background art fetch can be cancelled mid-read.
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

    NAVIDROME_TIMER("HTTP", "cover " + navidrome::dbg::scrubAuth(urlStr));
    std::wstring wurl = toWide(urlStr);

    URL_COMPONENTS uc = {};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256] = {}, path[4096] = {};
    uc.lpszHostName = host; uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path; uc.dwUrlPathLength = 4096;

    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) {
        return result; // Transport
    }

    WinHttpHandle sess(WinHttpOpen(L"foo_navidrome/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!sess) return result;

    WinHttpSetTimeouts(sess, 0, 15000, 15000, 30000);
    applySecureProtocols(sess);

    // Check abort before connect
    if (abort.is_aborting()) {
        result.cls = FetchClass::Aborted;
        return result;
    }

    WinHttpHandle conn(WinHttpConnect(sess, host, uc.nPort, 0));
    if (!conn) return result;

    DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    WinHttpHandle req(WinHttpOpenRequest(conn, L"GET", path,
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
    if (!req) return result;
    HINTERNET hReq = req;   // the rest of this function still reads `hReq`

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
        result.cls = FetchClass::Aborted;
        return result;
    }

    if (!WinHttpSendRequest(hReq, nullptr, 0, nullptr, 0, 0, 0) ||
        !WinHttpReceiveResponse(hReq, nullptr)) {
        NAVIDROME_WARN("HTTP", "cover request failed (winhttp err=" +
                       std::to_string(GetLastError()) + ")");
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
                result.cls = FetchClass::Aborted;
                return result;
            }

            // Check size limit
            if (body.size() > maxBytes || avail > maxBytes - body.size()) {
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

    NAVIDROME_LOG("HTTP", "cover HTTP " + std::to_string(result.httpStatus) +
                  " cls=" + std::to_string((int)result.cls) +
                  " bytes=" + std::to_string(result.body.size()));
    return result;
}
