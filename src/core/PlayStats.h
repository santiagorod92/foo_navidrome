#pragma once

#include <cstddef>
#include <ctime>
#include <string>

namespace navidrome {

constexpr const char* kPlayCountTag  = "NAVIDROME_PLAY_COUNT";
constexpr const char* kLastPlayedTag = "NAVIDROME_LAST_PLAYED";

struct PlayStats {
    bool        known = false;
    int         count = 0;
    std::string lastPlayed;
};

inline long long daysFromCivil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const long long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<long long>(doe) - 719468;
}

inline bool readIsoDigits(const std::string& s, std::size_t& i, int width, int& out) {
    if (i + static_cast<std::size_t>(width) > s.size()) return false;
    out = 0;
    for (int k = 0; k < width; ++k, ++i) {
        if (s[i] < '0' || s[i] > '9') return false;
        out = out * 10 + (s[i] - '0');
    }
    return true;
}

inline bool readIsoSeparator(const std::string& s, std::size_t& i, char sep) {
    if (i >= s.size() || s[i] != sep) return false;
    ++i;
    return true;
}

inline bool parseIsoTimestamp(const std::string& iso, long long& unixSeconds) {
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
    std::size_t i = 0;
    if (!readIsoDigits(iso, i, 4, y) || !readIsoSeparator(iso, i, '-') ||
        !readIsoDigits(iso, i, 2, mo) || !readIsoSeparator(iso, i, '-') ||
        !readIsoDigits(iso, i, 2, d) || !readIsoSeparator(iso, i, 'T') ||
        !readIsoDigits(iso, i, 2, h) || !readIsoSeparator(iso, i, ':') ||
        !readIsoDigits(iso, i, 2, mi) || !readIsoSeparator(iso, i, ':') ||
        !readIsoDigits(iso, i, 2, s))
        return false;
    if (y < 1970 || mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || s > 60) return false;
    if (i < iso.size() && (iso[i] == '.' || iso[i] == ',')) {
        ++i;
        while (i < iso.size() && iso[i] >= '0' && iso[i] <= '9') ++i;
    }
    long long offset = 0;
    if (i < iso.size() && (iso[i] == '+' || iso[i] == '-')) {
        const long long sign = iso[i] == '+' ? 1 : -1;
        ++i;
        int oh = 0, om = 0;
        if (!readIsoDigits(iso, i, 2, oh)) return false;
        if (i < iso.size() && iso[i] == ':') ++i;
        if (!readIsoDigits(iso, i, 2, om)) return false;
        offset = sign * (oh * 3600LL + om * 60LL);
    } else if (i < iso.size() && iso[i] != 'Z' && iso[i] != 'z') {
        return false;
    }
    unixSeconds = daysFromCivil(y, static_cast<unsigned>(mo), static_cast<unsigned>(d)) * 86400LL +
                  h * 3600LL + mi * 60LL + s - offset;
    return true;
}

inline std::string formatLocalTimestamp(long long unixSeconds) {
    const std::time_t t = static_cast<std::time_t>(unixSeconds);
    std::tm tm{};
#ifdef _WIN32
    if (localtime_s(&tm, &t) != 0) return std::string();
#else
    if (!localtime_r(&t, &tm)) return std::string();
#endif
    char buf[32];
    if (std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &tm) == 0) return std::string();
    return buf;
}

inline std::string lastPlayedDisplay(const std::string& iso) {
    long long secs = 0;
    return parseIsoTimestamp(iso, secs) ? formatLocalTimestamp(secs) : std::string();
}

struct PlayStatsMeta {
    std::string playCount;
    std::string lastPlayed;
};

inline PlayStatsMeta playStatsMeta(const PlayStats& p) {
    PlayStatsMeta m;
    if (p.count > 0) m.playCount = std::to_string(p.count);
    m.lastPlayed = lastPlayedDisplay(p.lastPlayed);
    return m;
}
}
