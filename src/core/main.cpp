#include "stdafx.h"
#include "NavidromePlaylistSync.h"
#include "NavidromeBrowserEnqueue.h"
#include "NavidromeRatingService.h"
#include "NavidromeLibraryService.h"
#include "NavidromeLyricsService.h"
#include "NavidromeAudioMuse.h"
#include "NavidromeLibraryPlatform.h"
#include "NavidromeDebugLog.h"
#include "NavidromeDiagnostics.h"
#include "NavidromeTrackInput.h"
#include "SubsonicTypes.h"
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
#include <thread>
#if __has_include("version_generated.h")
#  include "version_generated.h"
#endif
#ifndef COMPONENT_VERSION
#  define COMPONENT_VERSION "1.0.0"
#endif

DECLARE_COMPONENT_VERSION(
    "Navidrome Subsonic Client",
    COMPONENT_VERSION,
    "Streams music from Navidrome (or any Subsonic-compatible server) via the Subsonic API.\n"
    "\n"
    "Configuration: Preferences > Tools > Navidrome\n"
    "Browse: File > Open Navidrome Browser\n"
    "\n"
    "https://www.navidrome.org/"
);

VALIDATE_COMPONENT_FILENAME("foo_navidrome.dll");

FOOBAR2000_IMPLEMENT_CFG_VAR_DOWNGRADE;

static constexpr GUID guid_advcfg_refresh_on_start =
    { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01, 0x0e } };

static advconfig_checkbox_factory cfg_refresh_ratings_on_start(
    "Navidrome: refresh ratings on startup",
    guid_advcfg_refresh_on_start, advconfig_branch::guid_branch_tools,
    0.0, true);

bool navidrome::refreshRatingsOnStartEnabled() {
    return cfg_refresh_ratings_on_start.get();
}

static constexpr GUID guid_advcfg_verbose_log =
    { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x05, 0x01 } };

static advconfig_checkbox_factory cfg_verbose_log(
    "Navidrome: verbose logging (for bug reports)",
    guid_advcfg_verbose_log, advconfig_branch::guid_branch_tools,
    0.0, false);

namespace navidrome {

    extern cfg_string cfg_server_url;
    extern cfg_string cfg_stream_format;
    extern cfg_string cfg_custom_headers;
    extern cfg_string cfg_library_ids;
    extern cfg_var_modern::cfg_int  cfg_max_bitrate;
    extern cfg_var_modern::cfg_bool cfg_scrobble;
    extern cfg_var_modern::cfg_bool cfg_library_filter;
}

#ifndef _WIN32
#  include <sys/sysctl.h>
#endif

namespace {

std::string osVersion() {
#ifdef _WIN32
    using RtlGetVersionFn = LONG (WINAPI*)(OSVERSIONINFOW*);
    OSVERSIONINFOW v{}; v.dwOSVersionInfoSize = sizeof(v);
    if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll"))
        if (auto fn = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion")))
            if (fn(&v) == 0)
                return std::to_string(v.dwMajorVersion) + "." + std::to_string(v.dwMinorVersion) +
                       "." + std::to_string(v.dwBuildNumber);
    return "unknown";
#else
    char buf[64] = {};
    size_t len = sizeof(buf);
    if (sysctlbyname("kern.osproductversion", buf, &len, nullptr, 0) == 0) return buf;
    return "unknown";
#endif
}

std::string wineVersion() {
#ifdef _WIN32
    using WineVersionFn = const char* (*)();
    if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll"))
        if (auto fn = reinterpret_cast<WineVersionFn>(GetProcAddress(ntdll, "wine_get_version")))
            return fn();
#endif
    return {};
}

const char* buildArch() {
#if defined(_M_ARM64EC)
    return "ARM64EC";
#elif defined(_M_X64) || defined(__x86_64__)
    return "x64";
#elif defined(_M_IX86) || defined(__i386__)
    return "x86";
#elif defined(__aarch64__) || defined(_M_ARM64)
    return "arm64";
#else
    return "unknown";
#endif
}

const char* platformName() {
#ifdef _WIN32
    return "Windows";
#else
    return "macOS";
#endif
}

std::size_t csvCount(const char* csv) {
    std::size_t n = 0;
    bool inItem = false;
    for (const char* p = csv; *p; ++p) {
        if (*p == ',') { inItem = false; continue; }
        if (!inItem && *p != ' ') { ++n; inItem = true; }
    }
    return n;
}

class NavidromeLogInit : public initquit {
public:
    void on_init() override {
#ifndef NAVIDROME_DEBUG_LOG
        try {
            const pfc::string8 native =
                filesystem::g_get_native_path(core_api::pathInProfile("foo_navidrome.log"));
            navidrome::dbg::Logger::get().configure(native.c_str());
        } catch (const std::exception& e) {
            console::printf("Navidrome: can't open the component log: %s", e.what());
        }
#endif
        navidrome::dbg::Logger::get().setVerboseProbe([] { return cfg_verbose_log.get(); });
        NAVIDROME_NOTE("Env", std::string("foo_navidrome ") + COMPONENT_VERSION + " on " +
                       core_version_info::g_get_version_string() + ", " + platformName() + " " +
                       osVersion() + " (" + buildArch() + ")" +
                       (wineVersion().empty() ? "" : ", Wine " + wineVersion()) +
                       ", verbose=" + (cfg_verbose_log.get() ? "on" : "off"));
    }
};
static initquit_factory_t<NavidromeLogInit> g_navidrome_log_init;
}

std::string navidrome::componentLogPath() { return dbg::Logger::get().path(); }

std::string navidrome::collectDiagnostics() {
    DiagnosticsInfo d;
    d.componentVersion = COMPONENT_VERSION;
    d.foobarVersion    = core_version_info::g_get_version_string();
    d.platform         = platformName();
    d.osVersion        = osVersion();
    d.arch             = buildArch();
    d.wineVersion      = wineVersion();

    d.configured = libraryIsConfigured();
    d.serverUrl  = cfg_server_url.get().c_str();
    if (d.configured) {
        std::string err;
        d.serverReached = libraryServerInfo(d.server, err);
        d.serverError   = err;
    }

    d.transcodeFormat = cfg_stream_format.get().c_str();
    d.maxBitrate      = static_cast<int>(cfg_max_bitrate.get());
    d.scrobble        = cfg_scrobble.get();
    d.startupRefresh  = refreshRatingsOnStartEnabled();
    d.customHeaders   = cfg_custom_headers.get().length() > 0;
    d.libraryFilter   = cfg_library_filter.get();
    d.libraryCount    = csvCount(cfg_library_ids.get().c_str());
    d.audioMuse       = cfg_audiomuse_url.get().length() > 0;
    d.verboseLogging  = dbg::Logger::get().verbose();

    NAVIDROME_LOG("Env", std::string("diagnostics collected, server ") +
                  (d.configured ? (d.serverReached ? "reachable" : "unreachable") : "not configured"));
    d.logLines = dbg::Logger::get().recentLines();
    return buildDiagnostics(d);
}

namespace {

bool applyRatingFields(file_info& info, const navidrome::RatingUpdate& u) {
    auto have = [&info](const char* field) -> const char* {
        return info.meta_get_count_by_name(field) > 0 ? info.meta_get(field, 0) : nullptr;
    };
    auto put = [&info](const char* field, const char* value) {
        if (value) info.meta_set(field, value);
        else       info.meta_remove_field(field);
    };
    auto same = [](const char* a, const char* b) {
        return (a == nullptr) == (b == nullptr) && (a == nullptr || strcmp(a, b) == 0);
    };

    pfc::string8 rating;
    if (u.rating > 0) rating << u.rating;
    const char* wantRating  = rating.is_empty() ? nullptr : rating.c_str();
    const char* wantStarred = u.starred ? "1" : nullptr;

    if (same(have(navidrome::kRatingTag), wantRating) &&
        same(have(navidrome::kStarredTag), wantStarred)) return false;

    put(navidrome::kRatingTag,  wantRating);
    put(navidrome::kStarredTag, wantStarred);
    return true;
}
}

void navidrome::syncRatingsToPlaylists(std::vector<RatingUpdate> updates) {
    if (updates.empty()) return;

    std::unordered_map<std::string, RatingUpdate> bySongId;
    for (auto& u : updates) {
        if (!u.songId.empty()) bySongId[u.songId] = u;
    }
    if (bySongId.empty()) return;

    fb2k::inMainThread([bySongId = std::move(bySongId)] {
        auto pm = playlist_manager::get();
        auto hints = metadb_hint_list_v3::create();
        size_t touched = 0;

        const t_size playlistCount = pm->get_playlist_count();
        for (t_size pl = 0; pl < playlistCount; ++pl) {
            metadb_handle_list items;
            pm->playlist_get_all_items(pl, items);
            for (t_size i = 0; i < items.get_count(); ++i) {
                metadb_handle_ptr handle = items[i];
                const std::string songId = navidrome::trackIdFromURI(handle->get_path());
                if (songId.empty()) continue;
                auto it = bySongId.find(songId);
                if (it == bySongId.end()) continue;

                file_info_impl info;
                if (!handle->get_info(info)) continue;
                if (!applyRatingFields(info, it->second)) continue;

                hints->add_hint_forced(handle, info, filestats_invalid, true);
                ++touched;
            }
        }

        if (touched > 0) hints->on_done();
    });
}

navidrome::PlaylistAlbumScan navidrome::scanPlaylistAlbums() {
    PlaylistAlbumScan scan;
    std::unordered_set<std::string> seen;

    auto pm = playlist_manager::get();
    const t_size playlistCount = pm->get_playlist_count();
    for (t_size pl = 0; pl < playlistCount; ++pl) {
        metadb_handle_list items;
        pm->playlist_get_all_items(pl, items);
        for (t_size i = 0; i < items.get_count(); ++i) {
            const std::string path = items[i]->get_path();
            if (navidrome::trackIdFromURI(path).empty()) continue;
            scan.entries++;

            const std::string albumId = navidrome::queryParamFromURI(path, "albumId");
            if (albumId.empty()) { scan.ungrouped++; continue; }
            if (seen.insert(albumId).second) scan.albumIds.push_back(albumId);
        }
    }
    return scan;
}

namespace navidrome {
    extern cfg_string cfg_custom_headers;
    extern cfg_string cfg_stream_format;
}

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
    openAt(0.0, abort);
    if (!m_decoder->can_seek()) {
        m_seekByReopen = libraryCanSeekByTimeOffset();
        NAVIDROME_LOG("Input", std::string("stream not seekable; seek by timeOffset ")
                      + (m_seekByReopen ? "enabled (transcodeOffset)" : "unavailable"));
    }
}

void navidrome::TrackDecoder::openAt(double offsetSeconds, abort_callback& abort) {
    NAVIDROME_LOG("Input", "decode_initialize  id=" + m_track.id + " suffix=" + m_track.suffix
                  + (offsetSeconds > 0 ? " timeOffset=" + std::to_string(offsetSeconds) : std::string()));
    const std::string url = libraryStreamURL(m_track.id, m_track.coverArtId, offsetSeconds);
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
    NAVIDROME_LOG("Input", "decoder ready");
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
    return m_decoder.is_valid() && m_decoder->get_dynamic_info(out, delta);
}

bool navidrome::TrackDecoder::getDynamicInfoTrack(file_info& out, double& delta) {
    return m_decoder.is_valid() && m_decoder->get_dynamic_info_track(out, delta);
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

namespace {

metadb_handle_list makeTrackHandles(
        const std::vector<navidrome::BrowserNodePtr>& nodes,
        const std::function<std::string(const std::string&)>& radioUrl) {
    using navidrome::BrowserNode;
    metadb_handle_list tracks;
    auto hints = metadb_io_v2::get()->create_hint_list();

    for (const auto& node : nodes) {
        if (!node) continue;
        metadb_handle_ptr handle;
        playable_location_impl loc;

        if (node->type == BrowserNode::Radio) {
            const std::string url = radioUrl ? radioUrl(node->id) : std::string();
            if (url.empty()) continue;
            loc.set_path(url.c_str());
            loc.set_subsong(0);
            metadb::get()->handle_create(handle, loc);
            tracks += handle;

            file_info_impl info;
            if (!node->displayName.empty()) info.meta_set("title", node->displayName.c_str());
            hints->add_hint(handle, info, filestats_invalid, true);
            continue;
        }

        const navidrome::TrackURI t = navidrome::trackURIFromNode(*node);
        const std::string uri = navidrome::buildTrackURI(t);
        if (uri.empty()) continue;

        loc.set_path(uri.c_str());
        loc.set_subsong(0);
        metadb::get()->handle_create(handle, loc);
        tracks += handle;

        file_info_impl info;
        navidrome::applyTrackInfo(t, info);
        hints->add_hint(handle, info, filestats_invalid, true);
    }
    hints->on_done();
    return tracks;
}

void playFrom(t_size pl, t_size first) {
    auto pm = playlist_manager::get();
    pm->set_active_playlist(pl);
    pm->set_playing_playlist(pl);
    pm->playlist_set_focus_item(pl, first);
    playback_control::get()->start(playback_control::track_command_play);
}
}

void navidrome::showBrowserQueueError(const std::string& msg) {
    console::printf("Navidrome Browser: %s", msg.c_str());
    popup_message::g_show(msg.c_str(), "Navidrome Browser", popup_message::icon_error);
}

std::size_t navidrome::enqueueBrowserNodes(
        const std::vector<BrowserNodePtr>& nodes,
        bool play,
        bool clearFirst,
        const std::function<std::string(const std::string&)>& radioUrl,
        std::string& statusOut) {
    if (nodes.empty()) { statusOut = "No songs selected"; return 0; }

    const metadb_handle_list tracks = makeTrackHandles(nodes, radioUrl);

    auto pm = playlist_manager::get();
    t_size pl = pm->get_active_playlist();
    if (pl == pfc_infinite) {
        pm->create_playlist("Navidrome", ~0, pfc_infinite);
        pl = pm->get_active_playlist();
    }
    if (clearFirst) pm->playlist_clear(pl);
    t_size insertPos = pm->playlist_get_item_count(pl);
    pm->playlist_add_items(pl, tracks, pfc::bit_array_false());

    if (play && tracks.get_count() > 0) {
        playFrom(pl, insertPos);

        if (nodes.size() == 1 && nodes[0] && nodes[0]->bookmarkPositionMs > 0)
            navidrome::seekWhenReady(nodes[0]->bookmarkPositionMs / 1000.0);
    }

    statusOut = "Added " + std::to_string(tracks.get_count()) + " tracks";
    return tracks.get_count();
}

std::size_t navidrome::playNodesInNewPlaylist(const std::vector<BrowserNodePtr>& nodes,
                                              const std::string& name) {
    const metadb_handle_list tracks = makeTrackHandles(nodes, nullptr);
    if (tracks.get_count() == 0) return 0;
    auto pm = playlist_manager::get();
    const t_size pl = pm->create_playlist(name.c_str(), pfc_infinite, pfc_infinite);
    if (pl == pfc_infinite) return 0;
    pm->playlist_add_items(pl, tracks, pfc::bit_array_false());
    playFrom(pl, 0);
    return tracks.get_count();
}

namespace {

static constexpr GUID guid_ctx_group =
    { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x03, 0x00 } };

static contextmenu_group_popup_factory g_ctx_group(
    guid_ctx_group, contextmenu_groups::root, "Navidrome", 0.0);

constexpr unsigned kRatingItems = 6;
constexpr unsigned kItemStar    = 6;
constexpr unsigned kItemUnstar  = 7;

class navidrome_context_menu : public contextmenu_item_simple {
public:
    GUID get_parent() override { return guid_ctx_group; }

    unsigned get_num_items() override { return 8; }

    void get_item_name(unsigned index, pfc::string_base& out) override {
        if (index == kItemStar)   { out = "Star";      return; }
        if (index == kItemUnstar) { out = "Unstar";    return; }
        if (index == 0)           { out = "No rating"; return; }
        pfc::string_formatter name;
        for (unsigned i = 0; i < index; ++i) name << "\xE2\x98\x85";
        out = name;
    }

    bool get_item_description(unsigned index, pfc::string_base& out) override {
        (void)index;
        out = "Applies to the selected Navidrome tracks, server-side.";
        return true;
    }

    GUID get_item_guid(unsigned index) override {
        GUID g = { 0xa1b2c3d4, 0x1111, 0x2222,
                   { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x03, 0x01 } };
        g.Data4[7] = static_cast<unsigned char>(0x01 + index);
        return g;
    }

    bool context_get_display(unsigned index, metadb_handle_list_cref data,
                             pfc::string_base& out, unsigned& flags,
                             const GUID& caller) override {
        (void)flags; (void)caller;
        bool anyOurs = false;
        for (t_size i = 0; i < data.get_count() && !anyOurs; ++i)
            anyOurs = !navidrome::trackIdFromURI(data[i]->get_path()).empty();
        if (!anyOurs) return false;
        get_item_name(index, out);
        return true;
    }

    void context_command(unsigned index, metadb_handle_list_cref data,
                         const GUID& caller) override {
        (void)caller;
        std::vector<navidrome::RatingUpdate> updates;
        for (t_size i = 0; i < data.get_count(); ++i) {
            navidrome::RatingUpdate u;
            u.songId = navidrome::trackIdFromURI(data[i]->get_path());
            if (u.songId.empty()) continue;

            file_info_impl info;
            if (data[i]->get_info(info)) {
                if (info.meta_get_count_by_name(navidrome::kRatingTag) > 0)
                    u.rating = atoi(info.meta_get(navidrome::kRatingTag, 0));
                u.starred = info.meta_get_count_by_name(navidrome::kStarredTag) > 0;
            }

            if (index < kRatingItems) u.rating  = static_cast<int>(index);
            else                      u.starred = (index == kItemStar);
            updates.push_back(std::move(u));
        }
        if (updates.empty()) return;

        const bool rating = index < kRatingItems;
        std::thread([updates = std::move(updates), rating]() mutable {
            std::vector<navidrome::RatingUpdate> done;
            for (auto& u : updates) {
                const bool ok = rating ? navidrome::setRatingOnServer(u.songId, u.rating)
                                       : navidrome::setStarredOnServer(u.songId, u.starred);
                if (ok) done.push_back(std::move(u));
            }
            navidrome::syncRatingsToPlaylists(std::move(done));
        }).detach();
    }
};

static contextmenu_item_factory_t<navidrome_context_menu> g_navidrome_context_menu;
}

namespace navidrome {

static constexpr GUID guid_cfg_audiomuse_url =
    { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x04, 0x01 } };
static constexpr GUID guid_cfg_audiomuse_token =
    { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x04, 0x02 } };
static constexpr GUID guid_cfg_audiomuse_server =
    { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x04, 0x03 } };
static constexpr GUID guid_cfg_audiomuse_count =
    { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x04, 0x04 } };

cfg_string cfg_audiomuse_url(guid_cfg_audiomuse_url, "");
cfg_string cfg_audiomuse_token(guid_cfg_audiomuse_token, "");
cfg_string cfg_audiomuse_server(guid_cfg_audiomuse_server, "");
cfg_var_modern::cfg_int cfg_audiomuse_count(guid_cfg_audiomuse_count, audiomuse::kDefaultCount);
}

navidrome::audiomuse::Settings navidrome::audioMuseSettings() {
    audiomuse::Settings s;
    s.url    = cfg_audiomuse_url.get().c_str();
    s.token  = cfg_audiomuse_token.get().c_str();
    s.server = cfg_audiomuse_server.get().c_str();
    s.count  = audiomuse::clampCount(static_cast<int>(cfg_audiomuse_count.get()));
    return s;
}

namespace {

void reportError(const char* title, const std::string& msg) {
    console::printf("Navidrome: %s: %s", title, msg.c_str());
    popup_message::g_show(msg.c_str(), title, popup_message::icon_error);
}

using FetchFn = std::function<std::vector<navidrome::BrowserNodePtr>(
    threaded_process_status&, abort_callback&, std::string&)>;
using DoneFn = std::function<void(std::vector<navidrome::BrowserNodePtr>)>;

void runWithProgress(const char* title, std::string what, FetchFn fetch, DoneFn done) {
    struct State {
        std::vector<navidrome::BrowserNodePtr> nodes;
        std::string error;
    };
    auto state = std::make_shared<State>();
    auto cb = threaded_process_callback_lambda::create();
    cb->m_run = [state, fetch, what](threaded_process_status& status, abort_callback& abort) {
        status.set_item(what.c_str());
        try {
            state->nodes = fetch(status, abort, state->error);
        } catch (const exception_aborted&) {
            throw;
        } catch (const std::exception& e) {
            state->nodes.clear();
            state->error = e.what();
            NAVIDROME_WARN("AudioMuse", what + ": exception: " + e.what());
        }
    };
    const std::string titleStr = title;
    cb->m_on_done = [state, done, titleStr](threaded_process_callback::ctx_t, bool aborted) {
        if (aborted) { NAVIDROME_LOG("AudioMuse", titleStr + ": aborted"); return; }
        if (state->nodes.empty()) {
            reportError(titleStr.c_str(), state->error.empty() ? "No playable songs found." : state->error);
            return;
        }
        done(std::move(state->nodes));
    };
    threaded_process::g_run_modeless(cb,
        threaded_process::flag_show_abort | threaded_process::flag_show_item |
        threaded_process::flag_show_progress | threaded_process::flag_show_delayed,
        core_api::get_main_window(), title);
}

std::vector<navidrome::BrowserNodePtr> resolveWithProgress(
        const std::vector<navidrome::audiomuse::Track>& tracks,
        threaded_process_status& status, abort_callback& abort, std::string& err) {
    std::vector<navidrome::BrowserNodePtr> out;
    std::size_t missing = 0;
    for (std::size_t i = 0; i < tracks.size(); ++i) {
        abort.check();
        status.set_progress(i, tracks.size());
        std::size_t unresolved = 0;
        auto one = navidrome::audiomuse::resolveTracks(navidrome::libraryClient(), { tracks[i] }, unresolved);
        missing += unresolved;
        for (auto& n : one) out.push_back(std::move(n));
    }
    if (missing > 0)
        console::printf("Navidrome: AudioMuse-AI: %u of %u songs not found on the Navidrome server",
                        (unsigned)missing, (unsigned)tracks.size());
    if (out.empty() && err.empty() && !tracks.empty())
        err = "None of the songs AudioMuse-AI returned exist on the Navidrome server.";
    return out;
}

void runAudioMuse(navidrome::audiomuse::Kind kind, std::string subject,
                  std::function<std::vector<navidrome::audiomuse::Track>(std::string&)> query) {
    namespace am = navidrome::audiomuse;
    if (!navidrome::audioMuseSettings().configured()) {
        reportError("AudioMuse-AI", "Set the AudioMuse-AI server URL first:\n"
                    "Preferences > Tools > Navidrome > AudioMuse-AI.");
        return;
    }
    const std::string name = am::playlistName(kind, subject);
    runWithProgress(am::kindName(kind), am::kindName(kind) + std::string(": ") + subject,
        [query](threaded_process_status& status, abort_callback& abort, std::string& err) {
            auto tracks = query(err);
            abort.check();
            if (tracks.empty()) return std::vector<navidrome::BrowserNodePtr>();
            return resolveWithProgress(tracks, status, abort, err);
        },
        [name](std::vector<navidrome::BrowserNodePtr> nodes) {
            const std::size_t n = navidrome::playNodesInNewPlaylist(nodes, name);
            NAVIDROME_LOG("AudioMuse", "playlist \"" + name + "\": " + std::to_string(n) + " tracks");
        });
}
}

void navidrome::audioMuseTextSearchPrompt() {
    static std::string last;
    std::string q = last;
    if (!promptForText("AudioMuse-AI Text Search", "Describe the music (e.g. \"calm piano with rain\"):", q))
        return;
    if (q.empty()) return;
    last = q;
    runAudioMuse(audiomuse::Kind::TextSearch, q, [q](std::string& err) {
        return audiomuse::textSearch(audioMusePoster(), audioMuseSettings(), q, err);
    });
}

void navidrome::audioMuseInstantPlaylistPrompt() {
    static std::string last;
    std::string q = last;
    if (!promptForText("AudioMuse-AI Instant Playlist", "Ask for a playlist (e.g. \"90s road trip rock\"):", q))
        return;
    if (q.empty()) return;
    last = q;
    runAudioMuse(audiomuse::Kind::InstantPlaylist, q, [q](std::string& err) {
        return audiomuse::instantPlaylist(audioMusePoster(), audioMuseSettings(), q, err);
    });
}

void navidrome::audioMuseAlchemy(std::vector<audiomuse::AlchemySeed> seeds, std::string label) {
    runAudioMuse(audiomuse::Kind::Alchemy, label, [seeds = std::move(seeds)](std::string& err) {
        return audiomuse::alchemy(audioMusePoster(), audioMuseSettings(), seeds, err);
    });
}

namespace {

constexpr const char* kInstantMixPlaylist = "Instant Mix";

void playInstantMix(metadb_handle_ptr seed, const std::vector<navidrome::BrowserNodePtr>& similar) {
    auto pm = playlist_manager::get();
    const metadb_handle_list mix = makeTrackHandles(similar, nullptr);
    if (mix.get_count() == 0) return;

    metadb_handle_ptr playing;
    auto pc = playback_control::get();
    const bool seedPlaying = seed.is_valid() && pc->is_playing() &&
                             pc->get_now_playing(playing) && playing == seed;

    t_size pl = pm->find_playlist(kInstantMixPlaylist);
    t_size playingPl = pfc_infinite, playingIdx = pfc_infinite;
    const bool playingFromMix = pl != pfc_infinite &&
        pm->get_playing_item_location(&playingPl, &playingIdx) && playingPl == pl;

    if (pl == pfc_infinite) {
        pl = pm->create_playlist(kInstantMixPlaylist, pfc_infinite, pfc_infinite);
        if (pl == pfc_infinite) return;
    }

    if (seedPlaying && playingFromMix) {
        pm->playlist_remove_items(pl, pfc::bit_array_not(pfc::bit_array_one(playingIdx)));
        pm->playlist_add_items(pl, mix, pfc::bit_array_false());
    } else {
        pm->playlist_clear(pl);
        metadb_handle_list all;
        if (seed.is_valid()) all += seed;
        all += mix;
        pm->playlist_add_items(pl, all, pfc::bit_array_false());
    }
    pm->set_active_playlist(pl);
    NAVIDROME_LOG("UI", "Instant Mix: " + std::to_string(mix.get_count()) + " similar tracks" +
                  (seed.is_valid() ? " after the seed" : "") + (seedPlaying ? " (seed playing)" : ""));

    if (seedPlaying) {
        pm->set_playing_playlist(pl);
        pm->playlist_set_focus_item(pl, 0);
        return;
    }
    pm->set_playing_playlist(pl);
    if (seed.is_valid()) pm->playlist_execute_default_action(pl, 0);
    else                 playFrom(pl, 0);
}

std::vector<navidrome::BrowserNodePtr> audioMuseMix(const std::string& seedId,
                                                    navidrome::BrowserNode::Type seedType,
                                                    threaded_process_status& status,
                                                    abort_callback& abort, std::string& err) {
    namespace am = navidrome::audiomuse;
    const am::Settings settings = navidrome::audioMuseSettings();
    if (!settings.configured()) {
        NAVIDROME_LOG("UI", "Instant Mix: Navidrome returned nothing, AudioMuse-AI not configured");
        err = am::noSimilarMessage(am::MixFallback::NotConfigured, {});
        return {};
    }
    if (!am::canMixFrom(seedType)) {
        err = am::noSimilarMessage(am::MixFallback::Unsupported, {});
        return {};
    }
    std::string amErr;
    const auto tracks = am::similarTo(navidrome::audioMusePoster(), settings, seedId, seedType, amErr);
    abort.check();
    if (!amErr.empty()) {
        err = am::noSimilarMessage(am::MixFallback::Failed, amErr);
        return {};
    }
    auto nodes = navidrome::withoutSongId(resolveWithProgress(tracks, status, abort, amErr), seedId);
    if (nodes.empty()) {
        NAVIDROME_WARN("UI", "Instant Mix: AudioMuse-AI fallback found nothing for " + seedId);
        err = amErr.empty() ? am::noSimilarMessage(am::MixFallback::Empty, {})
                            : am::noSimilarMessage(am::MixFallback::Failed, amErr);
    }
    return nodes;
}

void runInstantMix(metadb_handle_ptr seed, std::string seedId, navidrome::BrowserNode::Type seedType,
                   std::string title) {
    const int count = navidrome::audiomuse::clampCount(static_cast<int>(navidrome::cfg_audiomuse_count.get()));
    NAVIDROME_LOG("UI", "Instant Mix: seed=" + seedId + " count=" + std::to_string(count));
    runWithProgress("Instant Mix", "Instant Mix: " + title,
        [seedId, seedType, count](threaded_process_status& status, abort_callback& abort, std::string& err) {
            auto nodes = navidrome::fetchSimilarSongs(navidrome::libraryClient(), seedId, count, err);
            abort.check();
            nodes = navidrome::withoutSongId(std::move(nodes), seedId);
            if (nodes.empty() && err.empty()) nodes = audioMuseMix(seedId, seedType, status, abort, err);
            return nodes;
        },
        [seed](std::vector<navidrome::BrowserNodePtr> nodes) { playInstantMix(seed, nodes); });
}

void startInstantMix(metadb_handle_list_cref data) {
    for (t_size i = 0; i < data.get_count(); ++i) {
        const std::string seedId = navidrome::trackIdFromURI(data[i]->get_path());
        if (seedId.empty()) continue;
        std::string title = seedId;
        file_info_impl info;
        if (data[i]->get_info(info) && info.meta_get_count_by_name("title") > 0)
            title = info.meta_get("title", 0);
        runInstantMix(data[i], seedId, navidrome::BrowserNode::Song, title);
        return;
    }
}
}

void navidrome::startInstantMix(const BrowserNodePtr& seed) {
    if (!seed || !isSimilarEligible(*seed)) return;
    metadb_handle_ptr seedHandle;
    if (seed->type == BrowserNode::Song) {
        const metadb_handle_list h = makeTrackHandles({ seed }, nullptr);
        if (h.get_count() > 0) seedHandle = h[0];
    }
    runInstantMix(seedHandle, seed->id, seed->type, seed->displayName.empty() ? seed->id : seed->displayName);
}

namespace {

void startAlchemy(metadb_handle_list_cref data) {
    std::vector<navidrome::audiomuse::AlchemySeed> seeds;
    std::string label;
    for (t_size i = 0; i < data.get_count(); ++i) {
        navidrome::audiomuse::AlchemySeed s;
        s.id = navidrome::trackIdFromURI(data[i]->get_path());
        if (s.id.empty()) continue;
        if (label.empty()) {
            file_info_impl info;
            if (data[i]->get_info(info) && info.meta_get_count_by_name("title") > 0)
                label = info.meta_get("title", 0);
        }
        seeds.push_back(std::move(s));
    }
    if (seeds.empty()) return;
    if (seeds.size() > 1) label += " + " + std::to_string(seeds.size() - 1) + " more";
    navidrome::audioMuseAlchemy(std::move(seeds), std::move(label));
}

constexpr unsigned kItemInstantMix = 0;
constexpr unsigned kItemAlchemy    = 1;

class navidrome_mix_context_menu : public contextmenu_item_simple {
public:
    GUID get_parent() override { return guid_ctx_group; }
    unsigned get_num_items() override { return 2; }

    void get_item_name(unsigned index, pfc::string_base& out) override {
        out = index == kItemInstantMix ? "Instant Mix" : "Song Alchemy (AudioMuse-AI)";
    }

    bool get_item_description(unsigned index, pfc::string_base& out) override {
        out = index == kItemInstantMix
            ? "Inserts songs similar to this track after it and plays them."
            : "Blends the selected tracks into a new AudioMuse-AI playlist.";
        return true;
    }

    GUID get_item_guid(unsigned index) override {
        GUID g = { 0xa1b2c3d4, 0x1111, 0x2222,
                   { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x03, 0x10 } };
        g.Data4[7] = static_cast<unsigned char>(0x10 + index);
        return g;
    }

    bool context_get_display(unsigned index, metadb_handle_list_cref data,
                             pfc::string_base& out, unsigned& flags, const GUID& caller) override {
        (void)flags; (void)caller;
        bool anyOurs = false;
        for (t_size i = 0; i < data.get_count() && !anyOurs; ++i)
            anyOurs = !navidrome::trackIdFromURI(data[i]->get_path()).empty();
        if (!anyOurs) return false;
        if (index == kItemAlchemy && !navidrome::audioMuseSettings().configured()) return false;
        get_item_name(index, out);
        return true;
    }

    void context_command(unsigned index, metadb_handle_list_cref data, const GUID& caller) override {
        (void)caller;
        if (index == kItemInstantMix) startInstantMix(data);
        else                          startAlchemy(data);
    }
};

static contextmenu_item_factory_t<navidrome_mix_context_menu> g_navidrome_mix_context_menu;

static constexpr GUID guid_mainmenu_audiomuse_group =
    { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x04, 0x10 } };
static constexpr GUID guid_mainmenu_audiomuse_search =
    { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x04, 0x11 } };
static constexpr GUID guid_mainmenu_audiomuse_playlist =
    { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x04, 0x12 } };

static mainmenu_group_popup_factory g_mainmenu_audiomuse_group(
    guid_mainmenu_audiomuse_group, mainmenu_groups::file, mainmenu_commands::sort_priority_dontcare,
    "AudioMuse-AI");

class navidrome_audiomuse_mainmenu : public mainmenu_commands {
public:
    t_uint32 get_command_count() override { return 2; }
    GUID get_command(t_uint32 i) override {
        if (i == 0) return guid_mainmenu_audiomuse_search;
        if (i == 1) return guid_mainmenu_audiomuse_playlist;
        throw pfc::exception_invalid_params();
    }
    void get_name(t_uint32 i, pfc::string_base& out) override {
        if (i == 0) { out = "Text Search..."; return; }
        if (i == 1) { out = "Instant Playlist..."; return; }
        throw pfc::exception_invalid_params();
    }
    bool get_description(t_uint32 i, pfc::string_base& out) override {
        if (i == 0) { out = "Find songs on Navidrome that sound like a description (AudioMuse-AI CLAP search)"; return true; }
        if (i == 1) { out = "Have AudioMuse-AI's assistant build a playlist from a request"; return true; }
        return false;
    }
    GUID get_parent() override { return guid_mainmenu_audiomuse_group; }
    void execute(t_uint32 i, service_ptr_t<service_base>) override {
        if (i == 0) navidrome::audioMuseTextSearchPrompt();
        else if (i == 1) navidrome::audioMuseInstantPlaylistPrompt();
    }
};

FB2K_SERVICE_FACTORY(navidrome_audiomuse_mainmenu);
}

namespace {

constexpr const char* kPlayQueuePlaylist = "Navidrome Play Queue";

void runPlayQueueTask(const char* title, std::function<std::string(abort_callback&)> work,
                      std::function<void()> done) {
    struct State { std::string error; };
    auto state = std::make_shared<State>();
    auto cb = threaded_process_callback_lambda::create();
    cb->m_run = [state, work](threaded_process_status& status, abort_callback& abort) {
        status.set_item("Contacting the Navidrome server...");
        try {
            state->error = work(abort);
        } catch (const exception_aborted&) {
            throw;
        } catch (const std::exception& e) {
            state->error = e.what();
        }
    };
    const std::string titleStr = title;
    cb->m_on_done = [state, done, titleStr](threaded_process_callback::ctx_t, bool aborted) {
        if (aborted) { NAVIDROME_LOG("PlayQueue", titleStr + ": aborted"); return; }
        if (!state->error.empty()) {
            NAVIDROME_WARN("PlayQueue", titleStr + " failed: " + state->error);
            reportError(titleStr.c_str(), state->error);
            return;
        }
        done();
    };
    threaded_process::g_run_modeless(cb,
        threaded_process::flag_show_abort | threaded_process::flag_show_item |
        threaded_process::flag_show_delayed,
        core_api::get_main_window(), title);
}

void savePlayQueueToServer() {
    if (!navidrome::libraryIsConfigured()) {
        reportError("Save Play Queue", "Set up the Navidrome server first: Preferences > Tools > Navidrome.");
        return;
    }
    auto pm = playlist_manager::get();
    auto pc = playback_control::get();
    t_size pl = pfc_infinite, current = pfc_infinite;
    double position = 0.0;
    if (pm->get_playing_item_location(&pl, &current)) {
        position = pc->playback_get_position();
    } else {
        pl = pm->get_active_playlist();
        if (pl == pfc_infinite) { reportError("Save Play Queue", "No playlist is active."); return; }
        current = pm->playlist_get_focus_item(pl);
    }
    metadb_handle_list items;
    pm->playlist_get_all_items(pl, items);
    std::vector<std::string> paths;
    paths.reserve(items.get_count());
    for (t_size i = 0; i < items.get_count(); ++i) paths.emplace_back(items[i]->get_path());

    const navidrome::PlayQueueSavePlan plan = navidrome::planPlayQueueSave(
        paths, current == pfc_infinite ? navidrome::kNoCurrentTrack : current, position);
    if (plan.songIds.empty()) {
        reportError("Save Play Queue", "The playlist has no Navidrome tracks to save.");
        return;
    }
    NAVIDROME_LOG("PlayQueue", "save: " + std::to_string(plan.songIds.size()) + " ids, current=" +
                  plan.currentId + " positionMs=" + std::to_string(static_cast<long long>(plan.positionMs)) +
                  " truncated=" + std::to_string(plan.truncated) +
                  " foreign=" + std::to_string(plan.skippedForeign));
    runPlayQueueTask("Save Play Queue",
        [plan](abort_callback&) {
            std::string err;
            if (navidrome::libraryClient().savePlayQueue(plan.songIds, plan.currentId, plan.positionMs, err))
                return std::string();
            return err.empty() ? std::string("The server rejected the play queue.") : err;
        },
        [plan]() {
            const std::string msg = navidrome::playQueueSaveSummary(plan);
            console::printf("Navidrome: %s", msg.c_str());
#ifdef __APPLE__
            fb2k::popup_toast::ptr toast;
            if (fb2k::popup_toast::tryGet(toast)) {
                toast->show_toast(msg.c_str(), {});
                NAVIDROME_LOG("PlayQueue", "confirmation: toast");
                return;
            }
#endif
            popup_message_v3::query_t q;
            q.title      = "Save Play Queue";
            q.msg        = msg.c_str();
            q.buttons    = popup_message_v3::buttonOK;
            q.defButton  = popup_message_v3::buttonOK;
            q.icon       = popup_message_v3::iconInformation;
            q.wndParent  = core_api::get_main_window();
            q.show();
            NAVIDROME_LOG("PlayQueue", "confirmation: dialog");
        });
}

void restorePlayQueueFromServer() {
    if (!navidrome::libraryIsConfigured()) {
        reportError("Restore Play Queue", "Set up the Navidrome server first: Preferences > Tools > Navidrome.");
        return;
    }
    auto plan = std::make_shared<navidrome::PlayQueueRestorePlan>();
    auto changedBy = std::make_shared<std::string>();
    runPlayQueueTask("Restore Play Queue",
        [plan, changedBy](abort_callback& abort) {
            navidrome::PlayQueue queue;
            std::string err;
            if (!navidrome::libraryClient().getPlayQueue(queue, err))
                return err.empty() ? std::string("Could not read the play queue.") : err;
            abort.check();
            *plan = navidrome::planPlayQueueRestore(queue);
            *changedBy = queue.changedBy;
            if (plan->nodes.empty()) return std::string("The server has no saved play queue.");
            return std::string();
        },
        [plan, changedBy]() {
            const metadb_handle_list tracks = makeTrackHandles(plan->nodes, nullptr);
            if (tracks.get_count() == 0) return;
            auto pm = playlist_manager::get();
            t_size pl = pm->find_playlist(kPlayQueuePlaylist);
            if (pl == pfc_infinite) pl = pm->create_playlist(kPlayQueuePlaylist, pfc_infinite, pfc_infinite);
            if (pl == pfc_infinite) return;
            pm->playlist_clear(pl);
            pm->playlist_add_items(pl, tracks, pfc::bit_array_false());
            const t_size start = (std::min)(static_cast<t_size>(plan->startIndex), tracks.get_count() - 1);
            playFrom(pl, start);
            if (plan->positionSeconds > 0) navidrome::seekWhenReady(plan->positionSeconds);
            NAVIDROME_LOG("PlayQueue", "restored " + std::to_string(tracks.get_count()) + " tracks, start=" +
                          std::to_string(start) + " position=" + std::to_string(plan->positionSeconds) +
                          (changedBy->empty() ? std::string() : " changedBy=" + *changedBy));
        });
}

static constexpr GUID guid_mainmenu_playqueue_save =
    { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x06, 0x01 } };
static constexpr GUID guid_mainmenu_playqueue_restore =
    { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x06, 0x02 } };

class navidrome_playqueue_mainmenu : public mainmenu_commands {
public:
    t_uint32 get_command_count() override { return 2; }
    GUID get_command(t_uint32 i) override {
        if (i == 0) return guid_mainmenu_playqueue_save;
        if (i == 1) return guid_mainmenu_playqueue_restore;
        throw pfc::exception_invalid_params();
    }
    void get_name(t_uint32 i, pfc::string_base& out) override {
        if (i == 0) { out = "Save Play Queue to Navidrome"; return; }
        if (i == 1) { out = "Restore Play Queue from Navidrome"; return; }
        throw pfc::exception_invalid_params();
    }
    bool get_description(t_uint32 i, pfc::string_base& out) override {
        if (i == 0) { out = "Save the playing (or active) playlist and position as your Navidrome play queue"; return true; }
        if (i == 1) { out = "Load your Navidrome play queue, saved by any Subsonic client, and resume it"; return true; }
        return false;
    }
    GUID get_parent() override { return mainmenu_groups::file; }
    void execute(t_uint32 i, service_ptr_t<service_base>) override {
        if (i == 0) savePlayQueueToServer();
        else if (i == 1) restorePlayQueueFromServer();
    }
};

FB2K_SERVICE_FACTORY(navidrome_playqueue_mainmenu);
}

namespace {

class navidrome_rating_api_impl : public navidrome::navidrome_rating_api {
public:
    bool is_navidrome_track(const metadb_handle_ptr& track) override {
        return track.is_valid() && !navidrome::trackIdFromURI(track->get_path()).empty();
    }

    void set_rating_async(const metadb_handle_ptr& track, int stars) override {
        if (track.is_empty()) return;
        std::string songId = navidrome::trackIdFromURI(track->get_path());
        if (songId.empty()) return;
        if (stars < 0) stars = 0;
        if (stars > 5) stars = 5;

        navidrome::RatingUpdate u;
        u.songId = songId;
        u.rating = stars;
        file_info_impl info;
        if (track->get_info(info))
            u.starred = info.meta_get_count_by_name(navidrome::kStarredTag) > 0;

        std::thread([u = std::move(u)]() mutable {
            if (navidrome::setRatingOnServer(u.songId, u.rating))
                navidrome::syncRatingsToPlaylists({ std::move(u) });
        }).detach();
    }
};

static service_factory_single_t<navidrome_rating_api_impl> g_navidrome_rating_api_factory;

class navidrome_library_api_impl : public navidrome::navidrome_library_api {
public:
    bool is_configured() override { return navidrome::libraryIsConfigured(); }

    bool list_albums(navidrome::library_album_sink& sink, abort_callback& abort,
                     pfc::string_base& errorOut) override {
        std::string err;
        size_t n = 0;
        const bool ok = navidrome::listLibraryAlbums(
            navidrome::libraryClient(), [&abort] { return abort.is_aborting(); },
            [&](const navidrome::Album& al) {
                sink.on_album(al.id.c_str(), al.name.c_str(), al.artist.c_str(), al.artistId.c_str(),
                              al.coverArtId.c_str(), al.year, al.songCount);
                ++n;
            }, err);
        if (!ok) {
            NAVIDROME_WARN("Library", "list_albums failed after " + std::to_string(n) + " albums: " + err);
            errorOut = err.c_str();
            return false;
        }
        NAVIDROME_LOG("Library", "list_albums: " + std::to_string(n) + " albums");
        return true;
    }

    album_art_data_ptr fetch_cover(const char* coverArtId, int size, abort_callback& abort) override {
        const std::string id = coverArtId ? coverArtId : "";
        if (id.empty()) throw exception_album_art_not_found();
        const std::vector<uint8_t> bytes = navidrome::libraryFetchCover(id, size, abort);
        abort.check();
        if (bytes.empty()) throw exception_album_art_not_found();
        return album_art_data_impl::g_create(bytes.data(), bytes.size());
    }

    void play_album(const char* albumId, bool replace, bool play) override {
        const std::string id = albumId ? albumId : "";
        if (id.empty()) return;
        std::thread([id, replace, play]() {
            std::string err;
            std::vector<navidrome::BrowserNodePtr> nodes;
            for (const auto& s : navidrome::libraryClient().getSongsForAlbum(id, err))
                nodes.push_back(navidrome::makeSongNode(s));
            enqueue(std::move(nodes), replace, play, "album " + id, err);
        }).detach();
    }

    void play_artist(const char* artistId, bool replace, bool play) override {
        const std::string id = artistId ? artistId : "";
        if (id.empty()) return;
        std::thread([id, replace, play]() {
            std::string err;
            auto nodes = navidrome::collectArtistSongs(navidrome::libraryClient(), id, err);
            enqueue(std::move(nodes), replace, play, "artist " + id, err);
        }).detach();
    }

private:
    static void enqueue(std::vector<navidrome::BrowserNodePtr> nodes, bool replace, bool play,
                        const std::string& what, const std::string& err) {
        if (nodes.empty()) {
            NAVIDROME_WARN("Library", "play " + what + ": no tracks" + (err.empty() ? "" : " (" + err + ")"));
            return;
        }
        fb2k::inMainThread([nodes = std::move(nodes), replace, play]() {
            std::string status;
            navidrome::enqueueBrowserNodes(nodes, play, replace,
                                           [](const std::string&) { return std::string(); }, status);
        });
    }
};

static service_factory_single_t<navidrome_library_api_impl> g_navidrome_library_api_factory;

class navidrome_lyrics_api_impl : public navidrome::navidrome_lyrics_api {
public:
    bool is_navidrome_track(const metadb_handle_ptr& track) override {
        return track.is_valid() && !navidrome::trackIdFromURI(track->get_path()).empty();
    }

    bool get_lyrics(const metadb_handle_ptr& track, navidrome::lyrics_sink& sink,
                    abort_callback& abort, pfc::string_base& errorOut) override {
        if (!is_navidrome_track(track) || !navidrome::libraryIsConfigured()) return false;
        abort.check();
        std::string err;
        const navidrome::Lyrics l =
            navidrome::lyricsForTrackURI(navidrome::libraryClient(), track->get_path(), err);
        abort.check();
        if (!err.empty()) {
            NAVIDROME_WARN("Lyrics", "get_lyrics failed: " + err);
            errorOut = err.c_str();
            return false;
        }
        if (l.empty()) return false;
        sink.on_begin(l.synced, static_cast<unsigned>(l.lines.size()));
        for (const auto& line : l.lines)
            sink.on_line(static_cast<int>(line.startMs), line.text.c_str());
        return true;
    }
};

static service_factory_single_t<navidrome_lyrics_api_impl> g_navidrome_lyrics_api_factory;
}
