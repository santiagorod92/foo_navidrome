#pragma once
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cwchar>
#include <deque>
#include <exception>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace navidrome {

namespace dbg {

inline std::string scrubAuth(std::string s) {
    for (size_t scheme = s.find("://"); scheme != std::string::npos;
         scheme = s.find("://", scheme + 3)) {
        const size_t hostStart = scheme + 3;
        const size_t end = s.find_first_of("/?# \t\r\n", hostStart);
        const size_t at = s.find('@', hostStart);
        if (at != std::string::npos && (end == std::string::npos || at < end))
            s.replace(hostStart, at - hostStart, "***");
    }
    for (const char* key : { "t=", "s=", "p=", "u=", "apiKey=" }) {
        const size_t keyLen = std::char_traits<char>::length(key);
        size_t pos = 0;
        while ((pos = s.find(key, pos)) != std::string::npos) {
            if (pos == 0 || (s[pos - 1] != '?' && s[pos - 1] != '&')) { pos += keyLen; continue; }
            size_t val = pos + keyLen;
            size_t end = s.find_first_of("& \t\r\n", val);
            if (end == std::string::npos) end = s.size();
            s.replace(val, end - val, "***");
            pos = val + 3;
        }
    }
    return s;
}

inline int levelRank(const char* level) {
    switch (level[0]) {
        case 'N': case 'n': return 3;
        case 'E': case 'e': return 2;
        case 'W': case 'w': return 1;
        default:            return 0;
    }
}

inline unsigned threadTag() {
    static thread_local unsigned id =
        static_cast<unsigned>(std::hash<std::thread::id>{}(std::this_thread::get_id()) % 10000u);
    return id;
}

struct LocalTime { int year, month, day, hour, minute, second, ms; };

inline LocalTime localNow() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const std::time_t t = system_clock::to_time_t(now);
    std::tm lt{};
#ifdef _WIN32
    localtime_s(&lt, &t);
#else
    localtime_r(&t, &lt);
#endif
    const int ms = static_cast<int>(duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000);
    return { lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min, lt.tm_sec, ms };
}

inline std::string formatLine(const LocalTime& t, const char* level, const char* tag,
                              unsigned thread, const std::string& msg) {
    char head[64];
    std::snprintf(head, sizeof(head), "%02d:%02d:%02d.%03d  %-5s  %-8s  [t%04u] ",
                  t.hour, t.minute, t.second, t.ms, level, tag, thread);
    return head + msg;
}

inline std::string formatDate(const LocalTime& t) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", t.year, t.month, t.day);
    return buf;
}

class LineRing {
public:
    explicit LineRing(std::size_t capacity) : m_cap(capacity) {}
    void push(std::string line) {
        if (m_cap == 0) return;
        if (m_lines.size() == m_cap) m_lines.pop_front();
        m_lines.push_back(std::move(line));
    }
    std::vector<std::string> lines() const { return { m_lines.begin(), m_lines.end() }; }
private:
    std::size_t             m_cap;
    std::deque<std::string> m_lines;
};

#ifdef _WIN32
inline std::wstring widen(const std::string& s) {
    std::wstring out;
    for (std::size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        unsigned cp; int extra;
        if      (c < 0x80)           { cp = c;        extra = 0; }
        else if ((c >> 5) == 0x6)    { cp = c & 0x1F; extra = 1; }
        else if ((c >> 4) == 0xE)    { cp = c & 0x0F; extra = 2; }
        else if ((c >> 3) == 0x1E)   { cp = c & 0x07; extra = 3; }
        else                         { cp = 0xFFFD;   extra = 0; }
        ++i;
        for (int k = 0; k < extra && i < s.size(); ++k, ++i)
            cp = (cp << 6) | (static_cast<unsigned char>(s[i]) & 0x3F);
        if (cp >= 0x10000) {
            cp -= 0x10000;
            out += static_cast<wchar_t>(0xD800 + (cp >> 10));
            out += static_cast<wchar_t>(0xDC00 + (cp & 0x3FF));
        } else {
            out += static_cast<wchar_t>(cp);
        }
    }
    return out;
}
inline FILE* openFile(const std::string& p, const char* mode) {
    FILE* f = nullptr;
    const std::wstring wm(mode, mode + std::char_traits<char>::length(mode));
    if (_wfopen_s(&f, widen(p).c_str(), wm.c_str()) != 0) return nullptr;
    return f;
}
inline void removeFile(const std::string& p) { _wremove(widen(p).c_str()); }
inline void renameFile(const std::string& from, const std::string& to) {
    _wrename(widen(from).c_str(), widen(to).c_str());
}
#else
inline FILE* openFile(const std::string& p, const char* mode) { return std::fopen(p.c_str(), mode); }
inline void removeFile(const std::string& p) { std::remove(p.c_str()); }
inline void renameFile(const std::string& from, const std::string& to) {
    std::rename(from.c_str(), to.c_str());
}
#endif

inline std::string envValue(const char* name) {
#ifdef _WIN32
    char* buf = nullptr; size_t len = 0;
    if (_dupenv_s(&buf, &len, name) != 0 || !buf) return {};
    std::string v(buf);
    std::free(buf);
    return v;
#else
    const char* v = std::getenv(name);
    return v ? v : "";
#endif
}

class Logger {
public:
    static constexpr std::size_t kRingLines = 300;

    static Logger& get() { static Logger inst; return inst; }

    void configure(const std::string& path) {
        std::lock_guard<std::mutex> lk(m_mu);
        m_path = path;
        rotateIfNeeded(true);
        for (const auto& l : m_pending) append(l);
        m_pending.clear();
    }

    void setVerboseProbe(std::function<bool()> probe) {
        std::lock_guard<std::mutex> lk(m_mu);
        m_verbose = std::move(probe);
    }

    std::string path() const { std::lock_guard<std::mutex> lk(m_mu); return m_path; }

    bool verbose() const {
        std::lock_guard<std::mutex> lk(m_mu);
        return minLevelLocked() == 0;
    }

    std::vector<std::string> recentLines() const {
        std::lock_guard<std::mutex> lk(m_mu);
        return m_ring.lines();
    }

    void line(const char* level, const char* tag, const std::string& msg) {
        std::lock_guard<std::mutex> lk(m_mu);
        if (levelRank(level) < minLevelLocked()) return;
        if (!tagAllowed(tag)) return;

        const LocalTime now = localNow();
        const std::string date = formatDate(now);
        if (m_date.empty()) {
            char banner[96];
            std::snprintf(banner, sizeof(banner), "==== foo_navidrome session %s %02d:%02d:%02d ====",
                          date.c_str(), now.hour, now.minute, now.second);
            emit(std::string(), false);
            emit(banner, true);
        } else if (date != m_date) {
            emit("==== " + date + " ====", true);
        }
        m_date = date;
        emit(formatLine(now, level, tag, threadTag(), msg), true);
    }

private:
    Logger() {
#ifdef NAVIDROME_DEBUG_LOG
        m_devBuild = true;
#  ifdef _WIN32
        m_path = "Z:\\tmp\\foo_navidrome_debug.log";
#  else
        m_path = "/tmp/foo_navidrome_debug.log";
#  endif
        m_maxBytes = 8L * 1024 * 1024;
#endif
        const std::string lv = envValue("NAVIDROME_LOG_LEVEL");
        if (!lv.empty()) m_envLevel = levelRank(lv.c_str());
        if (std::string s = envValue("NAVIDROME_LOG_TAGS"); !s.empty()) {
            for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            m_tagAllow = ",";
            for (char c : s) if (c != ' ' && c != '\t') m_tagAllow += c;
            m_tagAllow += ",";
            if (m_tagAllow == ",,") m_tagAllow.clear();
        }
        if (const std::string mb = envValue("NAVIDROME_LOG_MAX_MB"); !mb.empty()) {
            long v = std::atol(mb.c_str());
            if (v > 0) m_maxBytes = v * 1024 * 1024;
        }
        if (m_devBuild) rotateIfNeeded(true);
    }

    int minLevelLocked() const {
        if (m_envLevel >= 0) return m_envLevel;
        if (m_devBuild) return 0;
        return (m_verbose && m_verbose()) ? 0 : 1;
    }

    bool tagAllowed(const char* tag) const {
        if (m_tagAllow.empty()) return true;
        std::string needle = ",";
        for (const char* p = tag; *p; ++p)
            needle += static_cast<char>(std::tolower(static_cast<unsigned char>(*p)));
        needle += ",";
        return m_tagAllow.find(needle) != std::string::npos;
    }

    void emit(const std::string& text, bool toRing) {
        if (toRing) m_ring.push(text);
        if (m_path.empty()) { m_pending.push_back(text); if (m_pending.size() > kRingLines) m_pending.pop_front(); return; }
        append(text);
    }

    void append(const std::string& text) {
        FILE* f = openFile(m_path, "ab");
        if (!f) return;
        std::fwrite(text.data(), 1, text.size(), f);
        std::fputc('\n', f);
        const long size = std::ftell(f);
        std::fclose(f);
        if (size > m_maxBytes) rotateIfNeeded(false);
    }

    void rotateIfNeeded(bool startup) {
        if (m_path.empty()) return;
        long size = 0;
        if (startup) {
            FILE* f = openFile(m_path, "rb");
            if (!f) return;
            std::fseek(f, 0, SEEK_END);
            size = std::ftell(f);
            std::fclose(f);
            if (size <= m_maxBytes) return;
        }
        const std::string old = m_path + ".1";
        removeFile(old);
        renameFile(m_path, old);
    }

    mutable std::mutex        m_mu;
    std::string               m_path;
    bool                      m_devBuild = false;
    int                       m_envLevel = -1;
    std::string               m_tagAllow;
    long                      m_maxBytes = 2L * 1024 * 1024;
    std::function<bool()>     m_verbose;
    std::string               m_date;
    LineRing                  m_ring{kRingLines};
    std::deque<std::string>   m_pending;
};

inline void line(const char* level, const char* tag, const std::string& msg) {
    Logger::get().line(level, tag, msg);
}

class ScopedTimer {
public:
    ScopedTimer(const char* tag, std::string label)
        : m_tag(tag), m_label(std::move(label)),
          m_start(std::chrono::steady_clock::now()) {}
    ~ScopedTimer() {
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                      std::chrono::steady_clock::now() - m_start).count();
        char buf[32];
        if (us >= 1000) std::snprintf(buf, sizeof(buf), "%.1fms", us / 1000.0);
        else            std::snprintf(buf, sizeof(buf), "%lldus", (long long)us);
        line("INFO", m_tag, m_label + " took " + buf);
    }
    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;
private:
    const char* m_tag;
    std::string m_label;
    std::chrono::steady_clock::time_point m_start;
};

#define NAVIDROME_LOG_CAT2(a, b) a##b
#define NAVIDROME_LOG_CAT(a, b)  NAVIDROME_LOG_CAT2(a, b)

#define NAVIDROME_LOG(tag, msg)  ::navidrome::dbg::line("INFO",  (tag), (msg))
#define NAVIDROME_WARN(tag, msg) ::navidrome::dbg::line("WARN",  (tag), (msg))
#define NAVIDROME_ERR(tag, msg)  ::navidrome::dbg::line("ERROR", (tag), (msg))
#define NAVIDROME_NOTE(tag, msg) ::navidrome::dbg::line("NOTE",  (tag), (msg))
#define NAVIDROME_TIMER(tag, label) \
    ::navidrome::dbg::ScopedTimer NAVIDROME_LOG_CAT(navidrome_timer_, __LINE__)((tag), (label))

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
}
}
