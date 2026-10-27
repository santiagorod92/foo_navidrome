#include "stdafx.h"
#include "NavidromeSdkGlue.h"
#include "NavidromePlaylistSync.h"
#include "NavidromeBrowserEnqueue.h"
#include "NavidromeAudioMuse.h"
#include "NavidromeLibraryPlatform.h"
#include "NavidromeDebugLog.h"
#include "NavidromeGuids.h"
#include "NavidromeDiagnostics.h"
#include "NavidromeTrackInput.h"
#include "SubsonicTypes.h"
#include "NavidromeSettings.h"
#include "NavidromeOfflineCache.h"
#include <SDK/cfg_var.h>
#include <SDK/http_client.h>
#include <SDK/metadb.h>
#include <SDK/playlist.h>
#include <SDK/playable_location.h>
#include <SDK/playback_control.h>
#include <SDK/advconfig.h>
#include <SDK/contextmenu.h>
#include <SDK/album_art_helpers.h>
#include <SDK/threaded_process.h>
#include <SDK/menu.h>
#include <SDK/popup_message.h>
#include <SDK/initquit.h>
#include <SDK/coreversion.h>
#include <helpers/advconfig_impl.h>
#include <atomic>
#include <chrono>
#include <unordered_map>
#include <unordered_set>
#include <cstring>
#include <cstdlib>
#include <mutex>
#include <thread>

void navidrome::applyTrackInfo(const TrackURI& t, file_info& info) {
    for (const auto& f : trackMetaFields(t)) info.meta_add(f.first.c_str(), f.second.c_str());
    if (t.duration > 0) info.set_length(t.duration);
    if (!t.suffix.empty()) info.info_set("codec", t.suffix.c_str());
    if (t.rating > 0) info.meta_set(kRatingTag, pfc::format_int(t.rating));
    if (t.starred) info.meta_set(kStarredTag, "1");

    const ReplayGain& rg = t.tags.replayGain;
    if (!rg.empty()) {
        replaygain_info r;
        if (rg.hasTrackGain) r.m_track_gain = static_cast<float>(rg.trackGain);
        if (rg.hasAlbumGain) r.m_album_gain = static_cast<float>(rg.albumGain);
        if (rg.hasTrackPeak) r.m_track_peak = static_cast<float>(rg.trackPeak);
        if (rg.hasAlbumPeak) r.m_album_peak = static_cast<float>(rg.albumPeak);
        info.set_replaygain(r);
    }
}

void navidrome::TrackDecoder::open(const TrackURI& track, unsigned flags, abort_callback& abort) {
    m_track = track;
    m_flags = flags;
    m_seekByReopen = false;
    if (openCached(abort)) return;
    openAt(0.0, abort);
    if (!m_decoder->can_seek()) {
        m_seekByReopen = libraryCore().canSeekByTimeOffset();
        NAVIDROME_LOG("Input", std::string("stream not seekable; seek by timeOffset ")
                      + (m_seekByReopen ? "enabled (transcodeOffset)" : "unavailable"));
    }
}

bool navidrome::TrackDecoder::openCached(abort_callback& abort) {
    const std::string cached = offlineCachedPath(m_track);
    if (cached.empty()) return false;
    const std::string path = "file://" + cached;
    try {
        service_ptr_t<input_decoder> decoder;
        input_entry::g_open_for_decoding(decoder, nullptr, path.c_str(), abort, true);
        if (decoder.is_empty()) throw exception_io_data();
        decoder->initialize(0, m_flags, abort);
        m_decoder = decoder;
        captureStreamInfo(abort);
    } catch (const exception_aborted&) {
        throw;
    } catch (const std::exception& e) {
        NAVIDROME_WARN("Input", "id=" + m_track.id + " offline copy unreadable (" + e.what() + "), streaming instead");
        return false;
    }
    NAVIDROME_LOG("Input", "id=" + m_track.id + " playing offline copy " + cached);
    return true;
}

void navidrome::TrackDecoder::openAt(double offsetSeconds, abort_callback& abort) {
    NAVIDROME_LOG("Input", "decode_initialize  id=" + m_track.id + " suffix=" + m_track.suffix
                  + (offsetSeconds > 0 ? " timeOffset=" + std::to_string(offsetSeconds) : std::string()));
    const std::string url = libraryCore().streamURL(m_track.id, m_track.coverArtId, offsetSeconds);
    if (url.empty()) {
        NAVIDROME_ERR("Input", "stream URL empty: not configured or id unknown");
        throw exception_io_data();
    }
    NAVIDROME_LOG("Input", "stream URL = " + dbg::scrubAuth(url));

    file::ptr httpFile;
    const auto headers = parseHeaderLines(cfg_custom_headers.get().c_str());
    if (!headers.empty()) {
        NAVIDROME_LOG("Input", "opening stream ourselves with "
                      + std::to_string(headers.size()) + " custom header(s)");
        http_request::ptr req = http_client::get()->create_request("GET");
        for (const auto& h : headers) req->add_header(h.c_str());
        httpFile = req->run(url.c_str(), abort);
    }

    const std::string effSuffix = effectiveStreamSuffix(cfg_stream_format.get().c_str(), m_track.suffix);
    std::string hint = url;
    if (httpFile.is_valid() && !effSuffix.empty()) hint = "track." + effSuffix;

    NAVIDROME_LOG("Input", "g_open_for_decoding  hint=" + dbg::scrubAuth(hint)
                  + (httpFile.is_valid() ? "  (own http file)" : "  (foobar opens url)"));
    service_ptr_t<input_decoder> decoder;
    try {
        input_entry::g_open_for_decoding(decoder, httpFile, hint.c_str(), abort, true);
        if (decoder.is_empty()) {
            NAVIDROME_ERR("Input", "g_open_for_decoding produced no decoder: unsupported/corrupt stream");
            throw exception_io_data();
        }
        decoder->initialize(0, m_flags, abort);
    } catch (const exception_io_not_found&) {
        brokenTrackRegistry().markBroken(m_track.id);
        NAVIDROME_WARN("Input", "id=" + m_track.id + " not found on server, marking broken for this session");
        throw;
    }
    m_decoder = decoder;
    captureStreamInfo(abort);
    NAVIDROME_LOG("Input", "decoder ready");
}

void navidrome::TrackDecoder::captureStreamInfo(abort_callback& abort) {
    m_streamInfo.clear();
    file_info_impl info;
    try {
        m_decoder->get_info(0, info, abort);
    } catch (const exception_aborted&) {
        throw;
    } catch (const std::exception& e) {
        NAVIDROME_WARN("Input", "id=" + m_track.id + " stream info unreadable (" + e.what() + ")");
        return;
    }
    for (const char* key : {"codec", "codec_profile", "encoding"})
        if (const char* v = info.info_get(key)) m_streamInfo.emplace_back(key, v);
    m_streamInfoPending = !m_streamInfo.empty();
    NAVIDROME_LOG("Input", "id=" + m_track.id + " stream codec="
                  + (info.info_get("codec") ? info.info_get("codec") : "?") + " (catalog suffix=" + m_track.suffix + ")");
}

bool navidrome::TrackDecoder::run(audio_chunk& chunk, abort_callback& abort) {
    if (m_decoder.is_empty()) return false;
    while (m_decoder->run(chunk, abort)) {
        if (m_skipSeconds <= 0.0) return true;
        const double dur = chunk.get_duration();
        if (dur <= m_skipSeconds) { m_skipSeconds -= dur; continue; }
        const auto skip = static_cast<t_size>(m_skipSeconds * chunk.get_srate());
        m_skipSeconds = 0.0;
        chunk.skip_first_samples(skip);
        return true;
    }
    return false;
}

void navidrome::TrackDecoder::seek(double seconds, abort_callback& abort) {
    if (m_decoder.is_empty()) return;
    if (!m_seekByReopen) {
        NAVIDROME_LOG("Input", "seek to " + std::to_string(seconds) + "s (decoder)");
        m_decoder->seek(seconds, abort);
        return;
    }
    if (seconds < 0) seconds = 0;
    const double whole = static_cast<double>(static_cast<long long>(seconds));
    NAVIDROME_LOG("Input", "seek by reopening at timeOffset=" + std::to_string(static_cast<long long>(whole)));
    openAt(whole, abort);
    m_skipSeconds = seconds - whole;
}

bool navidrome::TrackDecoder::canSeek() {
    return m_decoder.is_valid() && (m_seekByReopen || m_decoder->can_seek());
}

bool navidrome::TrackDecoder::getDynamicInfo(file_info& out, double& delta) {
    bool changed = m_decoder.is_valid() && m_decoder->get_dynamic_info(out, delta);
    if (m_streamInfoPending) {
        m_streamInfoPending = false;
        for (const auto& kv : m_streamInfo) {
            const char* cur = out.info_get(kv.first.c_str());
            if (cur != nullptr && kv.second == cur) continue;
            out.info_set(kv.first.c_str(), kv.second.c_str());
            changed = true;
        }
    }
    return changed;
}

bool navidrome::TrackDecoder::getDynamicInfoTrack(file_info& out, double& delta) {
    if (!m_decoder.is_valid() || !m_decoder->get_dynamic_info_track(out, delta)) return false;
    if (out.get_length() <= 0 && m_track.duration > 0) out.set_length(m_track.duration);
    return true;
}

void navidrome::TrackDecoder::onIdle(abort_callback& abort) {
    if (m_decoder.is_valid()) m_decoder->on_idle(abort);
}

void navidrome::seekWhenReady(double positionSeconds) {
    auto done = std::make_shared<std::atomic<bool>>(false);
    std::thread([positionSeconds, done]() {
        for (int i = 0; i < 30 && !done->load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            fb2k::inMainThread([positionSeconds, done]() {
                if (done->load()) return;
                auto pc = playback_control::get();
                if (!pc->is_playing() || !pc->playback_can_seek()) return;
                done->store(true);
                NAVIDROME_LOG("UI", "resume: seeking to " + std::to_string(positionSeconds) + "s");
                pc->playback_seek(positionSeconds);
            });
        }
    }).detach();
}
