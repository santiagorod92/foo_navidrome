#pragma once
// ---------------------------------------------------------------------------
// Real-time debug tracing for the local dev loops (cross-platform).
//
// Every line is appended to  foo_navidrome_debug.log  in the host's /tmp:
//   Windows/Wine : Z:\tmp\foo_navidrome_debug.log  (Wine's Z: == host /)
//   macOS        : /tmp/foo_navidrome_debug.log
// so it's readable without the GUI (View > Console needs eyes on the app).
// `make win-logs` / `make mac-logs` (scripts/navidrome-logs.sh) tail it colourised.
//
// Format written per line:
//     HH:MM:SS.mmm  LEVEL  TAG       message
// plus a  ==== trace session <timestamp> ====  banner on the first line of a
// process. The dev-build scripts truncate the file per build and prepend a
// ==== build <version> installed … ====  marker.
//
// Gated on NAVIDROME_DEBUG_LOG:
//   Windows : win-build-local.sh passes /DNAVIDROME_DEBUG_LOG=1; the vcxproj/CI
//             build never defines it.
//   macOS   : mac-dev-build.sh passes OTHER_CFLAGS=-DNAVIDROME_DEBUG_LOG=1;
//             mac-ci-build.sh (release) does not.
// When undefined the macros expand to a type-checked sizeof no-op — zero cost,
// nothing ships.
//
// Tags in use: UI (BrowserWindow / browser controller), HTTP (Subsonic client
// request + outcome), API (Subsonic status != ok), Input (input handler decode).
//
// Header-only on purpose — no .cpp, so it is NOT added to any build source list
// (foo_navidrome.vcxproj, win-build-local.sh, win-vm/build-mac.sh).
// ---------------------------------------------------------------------------
#include <string>

#ifdef NAVIDROME_DEBUG_LOG
#  include <cstdio>
#  ifdef _WIN32
// SYSTEMTIME / GetLocalTime come from windows.h — every Windows TU that includes
// this already has it (stdafx.h PCH, or win-build-local.sh's forced prefix).
#    define NAVIDROME_DEBUG_LOG_PATH "Z:\\tmp\\foo_navidrome_debug.log"
#  else
#    include <sys/time.h>
#    include <ctime>
#    define NAVIDROME_DEBUG_LOG_PATH "/tmp/foo_navidrome_debug.log"
#  endif
#endif

namespace navidrome {
namespace dbg {

#ifdef NAVIDROME_DEBUG_LOG

// Redact Subsonic auth values (t=token, s=salt, p=password, u=user) from a URL
// before it goes to a world-readable log file. Keeps the endpoint + other
// params intact so the line stays useful for debugging.
inline std::string scrubAuth(std::string s) {
    for (const char* key : { "t=", "s=", "p=", "u=" }) {
        size_t pos = 0;
        while ((pos = s.find(key, pos)) != std::string::npos) {
            if (pos != 0 && s[pos - 1] != '?' && s[pos - 1] != '&') { pos += 2; continue; }
            size_t val = pos + 2;
            size_t end = s.find('&', val);
            if (end == std::string::npos) end = s.size();
            s.replace(val, end - val, "***");
            pos = val + 3;
        }
    }
    return s;
}

inline void line(const char* level, const char* tag, const std::string& msg) {
    FILE* f = fopen(NAVIDROME_DEBUG_LOG_PATH, "a");
    if (!f) return;

    int Y, Mo, D, h, mi, s, ms;
#  ifdef _WIN32
    SYSTEMTIME st;
    GetLocalTime(&st);
    Y = st.wYear; Mo = st.wMonth; D = st.wDay;
    h = st.wHour; mi = st.wMinute; s = st.wSecond; ms = st.wMilliseconds;
#  else
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    struct tm lt;
    localtime_r(&tv.tv_sec, &lt);
    Y = lt.tm_year + 1900; Mo = lt.tm_mon + 1; D = lt.tm_mday;
    h = lt.tm_hour; mi = lt.tm_min; s = lt.tm_sec; ms = (int)(tv.tv_usec / 1000);
#  endif

    static bool banner = [&] {
        fprintf(f, "\n==== foo_navidrome trace session  %04d-%02d-%02d %02d:%02d:%02d ====\n",
                Y, Mo, D, h, mi, s);
        return true;
    }();
    (void)banner;

    fprintf(f, "%02d:%02d:%02d.%03d  %-5s  %-8s  %s\n", h, mi, s, ms, level, tag, msg.c_str());
    fclose(f);
}

#  define NAVIDROME_LOG(tag, msg)  ::navidrome::dbg::line("INFO",  (tag), (msg))
#  define NAVIDROME_WARN(tag, msg) ::navidrome::dbg::line("WARN",  (tag), (msg))
#  define NAVIDROME_ERR(tag, msg)  ::navidrome::dbg::line("ERROR", (tag), (msg))

#else  // !NAVIDROME_DEBUG_LOG — type-check the args, emit nothing.

inline std::string scrubAuth(std::string s) { return s; }
inline void line(const char*, const char*, const std::string&) {}

#  define NAVIDROME_LOG(tag, msg)  ((void)sizeof((tag), (msg)))
#  define NAVIDROME_WARN(tag, msg) ((void)sizeof((tag), (msg)))
#  define NAVIDROME_ERR(tag, msg)  ((void)sizeof((tag), (msg)))

#endif

} // namespace dbg
} // namespace navidrome
