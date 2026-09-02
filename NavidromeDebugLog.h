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
//     HH:MM:SS.mmm  LEVEL  TAG       [tNNNN] message
// where tNNNN is a short, stable per-thread id (many background workers log:
// rating refresh, search debounce, seek-when-ready, downloads). Plus a
//     ==== trace session <timestamp> ====  banner on the first line of a
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
// Runtime knobs (read once, at first log line of the process — no rebuild):
//   NAVIDROME_LOG_LEVEL = INFO | WARN | ERROR   (default INFO) — drop lines
//                         below the threshold before they touch the file.
//   NAVIDROME_LOG_TAGS  = comma list, e.g. "HTTP,Input" — when set, only those
//                         tags are written. Case-insensitive.
//   NAVIDROME_LOG_MAX_MB = integer (default 8) — if the file is already bigger
//                         than this when the process starts, it is truncated
//                         first (guards a long `make … ARGS=-a` session).
//
// Tags in use: UI (BrowserWindow / browser controller), HTTP (Subsonic client
// request + outcome), API (Subsonic status != ok), Input (input handler
// decode), Scrobble (play_callback), Art (cover-art extractor / cache),
// Rating (startup + live rating/star sync), Lyrics (ESLyric bridge),
// Bookmark (resume-position sync), Env (one-shot session/config dump),
// Timer (scoped-duration lines from NAVIDROME_TIMER).
//
// runGuarded(tag, what, fn) wraps a detached-thread / dispatch-block body in a
// try/catch so an uncaught exception logs instead of calling std::terminate and
// taking foobar2000 down with it. Active in every build (the crash guard is the
// point, not the logging); in a release build the log line is the no-op macro.
//
// Header-only on purpose — no .cpp, so it is NOT added to any build source list
// (foo_navidrome.vcxproj, win-build-local.sh, win-vm/build-mac.sh).
// ---------------------------------------------------------------------------
#include <exception>
#include <string>
#include <utility>

#ifdef NAVIDROME_DEBUG_LOG
#  include <cstdio>
#  include <cstdlib>
#  include <cctype>
#  include <chrono>
#  include <thread>
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

// INFO=0 WARN=1 ERROR=2 — first char of the level string is enough.
inline int levelRank(const char* level) {
    switch (level[0]) {
        case 'E': return 2;
        case 'W': return 1;
        default:  return 0;
    }
}

// A short, stable id for the calling thread so interleaved lines from
// concurrent workers can be told apart. Hash of std::thread::id, 4 digits.
inline unsigned threadTag() {
    static thread_local unsigned id =
        static_cast<unsigned>(std::hash<std::thread::id>{}(std::this_thread::get_id()) % 10000u);
    return id;
}

// Parsed-once view of the NAVIDROME_LOG_* environment knobs.
struct Filters {
    int         minLevel  = 0;      // NAVIDROME_LOG_LEVEL
    std::string tagAllow;           // ","-framed lowercase list, empty = allow all
    long        maxBytes  = 8 * 1024 * 1024;

    Filters() {
        if (const char* lv = std::getenv("NAVIDROME_LOG_LEVEL")) {
            if (lv[0] == 'E' || lv[0] == 'e') minLevel = 2;
            else if (lv[0] == 'W' || lv[0] == 'w') minLevel = 1;
        }
        if (const char* tg = std::getenv("NAVIDROME_LOG_TAGS")) {
            std::string s(tg);
            for (char& c : s) c = static_cast<char>(std::tolower((unsigned char)c));
            tagAllow = ",";
            for (char c : s) if (c != ' ' && c != '\t') tagAllow += c;
            tagAllow += ",";
            if (tagAllow == ",,") tagAllow.clear();
        }
        if (const char* mb = std::getenv("NAVIDROME_LOG_MAX_MB")) {
            long v = std::atol(mb);
            if (v > 0) maxBytes = v * 1024 * 1024;
        }
    }

    bool tagAllowed(const char* tag) const {
        if (tagAllow.empty()) return true;
        std::string needle = ",";
        for (const char* p = tag; *p; ++p) needle += static_cast<char>(std::tolower((unsigned char)*p));
        needle += ",";
        return tagAllow.find(needle) != std::string::npos;
    }
};

inline const Filters& filters() {
    static const Filters f;
    return f;
}

inline void line(const char* level, const char* tag, const std::string& msg) {
    const Filters& flt = filters();
    if (levelRank(level) < flt.minLevel) return;
    if (!flt.tagAllowed(tag)) return;

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
        // One-time size guard: a long `make … ARGS=-a` session (the dev scripts
        // truncate per build, but a bare tail -F over many rebuilds accretes)
        // gets cut back to nothing before the first line of this process.
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        if (sz > flt.maxBytes) {
            f = freopen(NAVIDROME_DEBUG_LOG_PATH, "w", f);
            if (f)
                fprintf(f, "==== log truncated (was %ld bytes, cap %ld) ====\n",
                        sz, flt.maxBytes);
        }
        if (f)
            fprintf(f, "\n==== foo_navidrome trace session  %04d-%02d-%02d %02d:%02d:%02d ====\n",
                    Y, Mo, D, h, mi, s);
        return true;
    }();
    (void)banner;
    if (!f) return;

    fprintf(f, "%02d:%02d:%02d.%03d  %-5s  %-8s  [t%04u] %s\n",
            h, mi, s, ms, level, tag, threadTag(), msg.c_str());
    fclose(f);
}

// RAII scoped timer: logs "<label> took <n>ms" (or "…µs" under 1ms) when the
// enclosing scope exits. Use NAVIDROME_TIMER(tag, label) to declare one.
class ScopedTimer {
public:
    ScopedTimer(const char* tag, std::string label)
        : m_tag(tag), m_label(std::move(label)),
          m_start(std::chrono::steady_clock::now()) {}
    ~ScopedTimer() {
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                      std::chrono::steady_clock::now() - m_start).count();
        char buf[32];
        if (us >= 1000) snprintf(buf, sizeof(buf), "%.1fms", us / 1000.0);
        else            snprintf(buf, sizeof(buf), "%lldus", (long long)us);
        line("INFO", m_tag, m_label + " took " + buf);
    }
    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;
private:
    const char* m_tag;
    std::string m_label;
    std::chrono::steady_clock::time_point m_start;
};

#  define NAVIDROME_LOG_CAT2(a, b) a##b
#  define NAVIDROME_LOG_CAT(a, b)  NAVIDROME_LOG_CAT2(a, b)

#  define NAVIDROME_LOG(tag, msg)  ::navidrome::dbg::line("INFO",  (tag), (msg))
#  define NAVIDROME_WARN(tag, msg) ::navidrome::dbg::line("WARN",  (tag), (msg))
#  define NAVIDROME_ERR(tag, msg)  ::navidrome::dbg::line("ERROR", (tag), (msg))
#  define NAVIDROME_TIMER(tag, label) \
       ::navidrome::dbg::ScopedTimer NAVIDROME_LOG_CAT(navidrome_timer_, __LINE__)((tag), (label))

#else  // !NAVIDROME_DEBUG_LOG — type-check the args, emit nothing.

inline std::string scrubAuth(std::string s) { return s; }
inline void line(const char*, const char*, const std::string&) {}

#  define NAVIDROME_LOG(tag, msg)  ((void)sizeof((tag), (msg)))
#  define NAVIDROME_WARN(tag, msg) ((void)sizeof((tag), (msg)))
#  define NAVIDROME_ERR(tag, msg)  ((void)sizeof((tag), (msg)))
#  define NAVIDROME_TIMER(tag, label) ((void)sizeof((tag), (label)))

#endif

// Run `fn` and swallow+log any exception it throws. For detached std::thread
// bodies and dispatch_async blocks that call into HTTP / JSON / SDK code: an
// exception that escapes such a body is std::terminate (the whole app), and
// none of these paths has anything useful to do on failure anyway.
template <class Fn>
inline void runGuarded(const char* tag, const char* what, Fn&& fn) {
    try {
        std::forward<Fn>(fn)();
    } catch (const std::exception& e) {
        line("ERROR", tag, std::string("uncaught exception in ") + what + ": " + e.what());
    } catch (...) {
        line("ERROR", tag, std::string("uncaught non-std exception in ") + what);
    }
}

} // namespace dbg
} // namespace navidrome
