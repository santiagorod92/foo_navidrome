#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "SubsonicModels.h"
#include "SubsonicErrors.h"
#include "TrackUri.h"
#include "LibraryFilter.h"
#include "Json.h"
#include "SubsonicParsers.h"

namespace navidrome {

constexpr std::size_t kPlaylistChunkSize = 50;

inline std::string streamTranscodeParams(const std::string& format, int maxBitRate) {
    std::string out;
    if (!format.empty())  out += "&format=" + format;
    if (maxBitRate > 0)   out += "&maxBitRate=" + std::to_string(maxBitRate);
    return out;
}

struct StreamFormatOption { const char* label; const char* value; };

inline const std::vector<StreamFormatOption>& streamFormatOptions() {
    static const std::vector<StreamFormatOption> k = {
        { "Server default",            ""     },
        { "Original (no transcoding)", "raw"  },
        { "MP3",                       "mp3"  },
        { "Opus",                      "opus" },
        { "AAC",                       "aac"  },
        { "FLAC (lossless)",           "flac" },
        { "WAV (uncompressed)",        "wav"  },
    };
    return k;
}

inline const std::vector<int>& maxBitrateOptions() {
    static const std::vector<int> k = { 0, 64, 96, 128, 192, 256, 320 };
    return k;
}

constexpr int kScanPollIntervalMs = 1500;

constexpr const char* kPrefsAuthorLine = "Author: Santiago Rodriguez";
constexpr const char* kSourceCodeUrl   = "https://github.com/santiagorod92/foo_navidrome";

inline std::string effectiveStreamSuffix(const std::string& format,
                                         const std::string& trackSuffix) {
    if (format.empty() || format == "raw") return trackSuffix;
    return format;
}

inline double scrobbleSubmitThreshold(double trackLength) {
    if (trackLength <= 0.0) return 240.0;
    double half = trackLength * 0.5;
    return half < 240.0 ? half : 240.0;
}

inline std::vector<std::string> parseHeaderLines(const std::string& blob) {
    std::vector<std::string> out;
    std::string line;
    auto flush = [&]() {
        const char* ws = " \t\r\n";
        size_t b = line.find_first_not_of(ws);
        size_t e = line.find_last_not_of(ws);
        if (b != std::string::npos) {
            std::string trimmed = line.substr(b, e - b + 1);
            if (!trimmed.empty() && trimmed[0] != '#')
                out.push_back(trimmed);
        }
        line.clear();
    };
    for (char ch : blob) {
        if (ch == '\n') flush();
        else            line.push_back(ch);
    }
    flush();
    return out;
}

struct ScrobbleTracker {
    struct NewTrackActions {
        std::string refreshRatingId;
        std::string scrobbleNowId;
    };

    NewTrackActions onNewTrack(const std::string& rawUri, double lengthSec,
                               bool scrobbleEnabled) {
        songId_.clear();
        length_    = 0.0;
        submitted_ = false;

        NewTrackActions a;
        std::string id = trackIdFromURI(rawUri);
        if (id.empty()) return a;
        a.refreshRatingId = id;
        if (!scrobbleEnabled) return a;
        songId_ = id;
        length_ = lengthSec;
        a.scrobbleNowId = id;
        return a;
    }

    std::string onPlaybackTime(double timeSec) {
        if (songId_.empty() || submitted_) return {};
        if (timeSec < scrobbleSubmitThreshold(length_)) return {};
        submitted_ = true;
        return songId_;
    }

    void onStop() {
        songId_.clear();
        submitted_ = false;
    }

private:
    std::string songId_;
    double      length_    = 0.0;
    bool        submitted_ = false;
};

class BrokenTrackRegistry {
public:
    void markBroken(const std::string& songId) {
        if (songId.empty()) return;
        std::lock_guard<std::mutex> lock(mutex_);
        broken_.insert(songId);
    }
    bool isBroken(const std::string& songId) const {
        if (songId.empty()) return false;
        std::lock_guard<std::mutex> lock(mutex_);
        return broken_.count(songId) != 0;
    }

private:
    mutable std::mutex mutex_;
    std::unordered_set<std::string> broken_;
};

inline BrokenTrackRegistry& brokenTrackRegistry() {
    static BrokenTrackRegistry instance;
    return instance;
}

struct SessionEnv {
    std::string platform;
    bool        configured = false;
    std::string serverUrl;
    std::string transcodeFormat;
    int         maxBitrate = 0;
    bool        scrobble = false;
    bool        startupRefresh = false;
    bool        customHeaders = false;
};

inline std::string describeSessionEnv(const SessionEnv& e) {
    return "platform=" + e.platform
        + "  configured=" + (e.configured ? "yes" : "no")
        + "  server=" + e.serverUrl
        + "  transcode=" + (e.transcodeFormat.empty() ? "server-default" : e.transcodeFormat)
        + "  maxBitrate=" + std::to_string(e.maxBitrate)
        + "  scrobble=" + (e.scrobble ? "on" : "off")
        + "  startupRefresh=" + (e.startupRefresh ? "on" : "off")
        + "  customHeaders=" + (e.customHeaders ? "yes" : "no");
}
}
