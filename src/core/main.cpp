#include "stdafx.h"
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

static advconfig_checkbox_factory cfg_refresh_ratings_on_start(
    "Navidrome: refresh ratings on startup",
    navidrome::guids::advRefreshRatingsOnStart, advconfig_branch::guid_branch_tools,
    0.0, true);

bool navidrome::refreshRatingsOnStartEnabled() {
    return cfg_refresh_ratings_on_start.get();
}

static advconfig_checkbox_factory cfg_verbose_log(
    "Navidrome: verbose logging (for bug reports)",
    navidrome::guids::advVerboseLog, advconfig_branch::guid_branch_tools,
    0.0, false);

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

void navidrome::warnAuthRejectedOnce() {
    static std::once_flag once;
    std::call_once(once, [] { console::print(kAuthRejectedMessage); });
}

std::string navidrome::collectDiagnostics() {
    DiagnosticsInfo d;
    d.componentVersion = COMPONENT_VERSION;
    d.foobarVersion    = core_version_info::g_get_version_string();
    d.platform         = platformName();
    d.osVersion        = osVersion();
    d.arch             = buildArch();
    d.wineVersion      = wineVersion();

    d.configured = libraryCore().isConfigured();
    d.serverUrl  = cfg_server_url.get().c_str();
    if (d.configured) {
        std::string err;
        d.serverReached = libraryCore().serverInfo(d.server, err);
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
