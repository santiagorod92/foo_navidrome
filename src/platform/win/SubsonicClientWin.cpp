#include "stdafx.h"
#include "SubsonicClientWin.h"
#include "WinHttpClient.h"
#include "WinUi.h"
#include "../../core/MediaEnrichmentLogic.h"
#include "../../core/NavidromeDebugLog.h"
#include "../../core/NavidromeAudioMuse.h"
#include "../../core/NavidromeSettings.h"
#include "../../core/NavidromeDiagnostics.h"
#include <SDK/cfg_var.h>

#pragma comment(lib, "crypt32.lib")

namespace {

struct WinHttpTransport : navidrome::IHttpTransport {
    navidrome::HttpResult getOnce(const std::string& urlStr) override {
        using navidrome::ErrorKind;
        namespace w = navidrome::win;
        navidrome::HttpResult out;

        auto http = w::openWinHttpRequest(urlStr, L"GET", w::WinHttpTimeouts{},
                                          navidrome::cfg_custom_headers.get().c_str());
        if (!http.ok()) {
            out.error = { http.errorKind, 0, 0, http.error };
            return out;
        }
        DWORD e = 0;
        if (!w::sendWinHttpRequest(http.request, nullptr, e)) {
            ErrorKind kind = w::classifyWinHttpError(e);
            out.error = { kind, 0, 0, std::string(navidrome::errorKindName(kind)) +
                          " (winhttp err=" + std::to_string(e) + ")" };
            return out;
        }
        const DWORD status = w::winHttpStatus(http.request);
        ErrorKind kind = navidrome::httpStatusToErrorKind((int)status);
        if (kind != ErrorKind::None) {
            out.error = { kind, (int)status, 0, "HTTP " + std::to_string(status) };
            return out;
        }
        out.body = w::readWinHttpBody(http.request);
        out.error = { ErrorKind::None, (int)status, 0, {} };
        return out;
    }

    void onAuthRejected() override { navidrome::warnAuthRejectedOnce(); }
};

struct WinJsonPoster : navidrome::audiomuse::IJsonPoster {
    navidrome::HttpResult postJson(const std::string& urlStr, const std::string& body,
                                   const std::string& bearerToken, int timeoutMs) override {
        using navidrome::ErrorKind;
        namespace w = navidrome::win;
        navidrome::HttpResult out;

        std::string headers = "Content-Type: application/json\nAccept: application/json";
        if (!bearerToken.empty()) headers += "\nAuthorization: Bearer " + bearerToken;
        auto http = w::openWinHttpRequest(urlStr, L"POST", w::WinHttpTimeouts{15000, 30000, timeoutMs},
                                          headers);
        if (!http.ok()) {
            out.error = { http.errorKind, 0, 0,
                          http.errorKind == ErrorKind::Parse ? "Invalid AudioMuse-AI URL" : http.error };
            return out;
        }
        WinHttpSetTimeouts(http.request, 0, 15000, 30000, timeoutMs);
        DWORD responseTimeout = static_cast<DWORD>(timeoutMs);
        WinHttpSetOption(http.request, WINHTTP_OPTION_RECEIVE_RESPONSE_TIMEOUT,
                         &responseTimeout, sizeof(responseTimeout));
        DWORD e = 0;
        if (!w::sendWinHttpRequest(http.request, &body, e)) {
            ErrorKind kind = w::classifyWinHttpError(e);
            out.error = { kind, 0, 0, std::string(navidrome::errorKindName(kind)) +
                          " (winhttp err=" + std::to_string(e) + ")" };
            return out;
        }
        const DWORD status = w::winHttpStatus(http.request);
        out.body = w::readWinHttpBody(http.request);
        ErrorKind kind = navidrome::httpStatusToErrorKind((int)status);
        out.error = { kind, (int)status, 0,
                      kind == ErrorKind::None ? std::string() : "HTTP " + std::to_string(status) };
        return out;
    }
};

struct WinSettingsProvider : navidrome::ISettingsProvider {
    navidrome::SubsonicSettings load() const override {
        navidrome::SubsonicSettings s;
        s.serverUrl     = navidrome::cfg_server_url.get().c_str();
        s.username      = navidrome::cfg_username.get().c_str();
        s.password      = navidrome::cfg_password.get().c_str();
        s.salt          = navidrome::effectiveSalt(navidrome::cfg_salt.get().c_str());
        s.streamFormat  = navidrome::cfg_stream_format.get().c_str();
        s.maxBitrate    = static_cast<int>(navidrome::cfg_max_bitrate.get());
        s.libraryFilter = navidrome::cfg_library_filter.get();
        s.libraryIdsCsv = navidrome::cfg_library_ids.get().c_str();
        return s;
    }
};
}

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
    context.salt = effectiveSalt(cfg_salt.get().c_str());
    context.customHeaders = cfg_custom_headers.get().c_str();
    return context;
}

bool navidrome::SubsonicClientWin::ping(std::string& outError) { return m_core->ping(outError); }
bool navidrome::SubsonicClientWin::serverInfo(ServerInfo& out, std::string& outError) {
    return m_core->serverInfo(out, outError);
}

std::vector<navidrome::MusicFolder>
navidrome::SubsonicClientWin::getMusicFolders(std::string& outError) {
    return m_core->getMusicFolders(outError);
}
std::vector<navidrome::MusicFolder> navidrome::SubsonicClientWin::cachedMusicFolders() {
    return m_core->cachedMusicFolders();
}
void navidrome::SubsonicClientWin::refreshMusicFolders() { m_core->refreshMusicFolders(); }
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
std::vector<navidrome::Song>
navidrome::SubsonicClientWin::getAllSongs(std::string& outError) {
    return m_core->getAllSongs(outError);
}
navidrome::ArtistInfo
navidrome::SubsonicClientWin::getArtistInfo(const std::string& artistId, std::string& outError) {
    return m_core->getArtistInfo(artistId, outError);
}
std::vector<navidrome::Song>
navidrome::SubsonicClientWin::getTopSongs(const std::string& artistName, int count,
                                           std::string& outError) {
    return m_core->getTopSongs(artistName, count, outError);
}

navidrome::Lyrics
navidrome::SubsonicClientWin::getLyrics(const std::string& songId, const std::string& artist,
                                         const std::string& title, std::string& outError) {
    return m_core->getLyrics(songId, artist, title, outError);
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

std::vector<navidrome::PodcastChannel>
navidrome::SubsonicClientWin::getPodcastChannels(std::string& outError) {
    return m_core->getPodcastChannels(outError);
}
std::vector<navidrome::PodcastEpisode>
navidrome::SubsonicClientWin::getPodcastEpisodes(const std::string& channelId,
                                                  std::string& outError) {
    return m_core->getPodcastEpisodes(channelId, outError);
}
std::string navidrome::SubsonicClientWin::createPodcastChannel(const std::string& url,
                                                                 std::string& outError) {
    return m_core->createPodcastChannel(url, outError);
}
bool navidrome::SubsonicClientWin::deletePodcastChannel(const std::string& id,
                                                          std::string& outError) {
    return m_core->deletePodcastChannel(id, outError);
}

std::vector<navidrome::NowPlayingEntry>
navidrome::SubsonicClientWin::getNowPlaying(std::string& outError) {
    return m_core->getNowPlaying(outError);
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

std::string navidrome::SubsonicClientWin::streamURL(const std::string& songId,
                                                    const std::string& coverArtId,
                                                    double timeOffsetSeconds) {
    return m_core->streamURL(songId, coverArtId, timeOffsetSeconds);
}

bool navidrome::SubsonicClientWin::canSeekByTimeOffset() {
    return m_core->canSeekByTimeOffset();
}

bool navidrome::SubsonicClientWin::getPlayQueue(PlayQueue& out, std::string& outError) {
    return m_core->getPlayQueue(out, outError);
}

bool navidrome::SubsonicClientWin::savePlayQueue(const std::vector<std::string>& songIds,
                                                 const std::string& currentId,
                                                 double positionMs, std::string& outError) {
    return m_core->savePlayQueue(songIds, currentId, positionMs, outError);
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

bool navidrome::SubsonicClientWin::httpDownloadToFile(const std::string& urlStr,
                                                       const std::wstring& destPath,
                                                       std::string& outError) const {
    namespace w = navidrome::win;
    const std::string safeUrl = navidrome::dbg::scrubAuth(urlStr);
    NAVIDROME_TIMER("HTTP", "download " + safeUrl);

    auto http = w::openWinHttpRequest(urlStr, L"GET", w::WinHttpTimeouts{15000, 15000, 300000},
                                      cfg_custom_headers.get().c_str());
    if (!http.ok()) {
        outError = http.error;
        NAVIDROME_ERR("HTTP", "download open failed (" + outError + "): " + safeUrl);
        return false;
    }

    bool ok = false;
    DWORD e = 0;
    if (w::sendWinHttpRequest(http.request, nullptr, e)) {
        const DWORD status = w::winHttpStatus(http.request);
        if (status != 200) {
            outError = "HTTP " + std::to_string(status);
        } else {
            HANDLE hFile = CreateFileW(destPath.c_str(), GENERIC_WRITE, 0, nullptr,
                                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hFile == INVALID_HANDLE_VALUE) {
                outError = "Cannot create file (err=" + std::to_string(GetLastError()) + ")";
            } else {
                bool writeFailed = false;
                ok = w::readWinHttpBody(http.request, [&](const char* p, DWORD n) {
                    DWORD written = 0;
                    if (!WriteFile(hFile, p, n, &written, nullptr) || written != n) {
                        outError = "Write failed (err=" + std::to_string(GetLastError()) + ")";
                        writeFailed = true;
                        return false;
                    }
                    return true;
                });
                if (!ok && !writeFailed)
                    outError = "Read failed (err=" + std::to_string(GetLastError()) + ")";
                CloseHandle(hFile);
                if (!ok) DeleteFileW(destPath.c_str());
            }
        }
    } else {
        outError = "Request failed (err=" + std::to_string(e) + ")";
    }

    if (ok)
        NAVIDROME_LOG("HTTP", "download ok -> " + win::wToU8(destPath));
    else
        NAVIDROME_ERR("HTTP", "download failed (" + outError + "): " + safeUrl);
    return ok;
}

navidrome::SubsonicClientWin::BinaryFetchResult
navidrome::SubsonicClientWin::httpGetBinary(
        const SubsonicRequestContext& context,
        const std::string& urlStr,
        std::size_t maxBytes,
        abort_callback& abort) const {
    BinaryFetchResult result;
    result.cls = FetchClass::Transport;
    result.httpStatus = 0;

    namespace w = navidrome::win;
    NAVIDROME_TIMER("HTTP", "cover " + navidrome::dbg::scrubAuth(urlStr));

    if (abort.is_aborting()) {
        result.cls = FetchClass::Aborted;
        return result;
    }
    auto http = w::openWinHttpRequest(urlStr, L"GET", w::WinHttpTimeouts{}, context.customHeaders);
    if (!http.ok()) return result;
    HINTERNET hReq = http.request;

    if (abort.is_aborting()) {
        result.cls = FetchClass::Aborted;
        return result;
    }

    DWORD sendErr = 0;
    if (!w::sendWinHttpRequest(hReq, nullptr, sendErr)) {
        NAVIDROME_WARN("HTTP", "cover request failed (winhttp err=" + std::to_string(sendErr) + ")");
        return result;
    }

    const DWORD status = w::winHttpStatus(hReq);
    result.httpStatus = status;
    result.cls = classifyHttpStatus(status);
    result.contentType = w::winHttpContentType(hReq);

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

            if (abort.is_aborting()) {
                result.cls = FetchClass::Aborted;
                return result;
            }

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

navidrome::audiomuse::IJsonPoster& navidrome::audioMusePoster() {
    static WinJsonPoster inst;
    return inst;
}
