#pragma once

#include "SubsonicTypes.h"

#include <algorithm>
#include <string>

namespace navidrome {

enum class LyricsPanelStatus {
    NothingPlaying,
    NotNavidrome,
    NotConfigured,
    Loading,
    Failed,
    NoLyrics,
    Showing,
};

inline LyricsPanelStatus lyricsPanelStatusForTrack(const std::string& uri, bool configured) {
    if (uri.empty()) return LyricsPanelStatus::NothingPlaying;
    if (trackIdFromURI(uri).empty()) return LyricsPanelStatus::NotNavidrome;
    if (!configured) return LyricsPanelStatus::NotConfigured;
    return LyricsPanelStatus::Loading;
}

inline LyricsPanelStatus lyricsPanelStatusForResult(const Lyrics& lyrics, const std::string& error) {
    if (!error.empty()) return LyricsPanelStatus::Failed;
    if (lyrics.empty()) return LyricsPanelStatus::NoLyrics;
    return LyricsPanelStatus::Showing;
}

inline const char* lyricsPanelMessage(LyricsPanelStatus status) {
    switch (status) {
        case LyricsPanelStatus::NothingPlaying: return "Nothing playing";
        case LyricsPanelStatus::NotNavidrome:   return "Lyrics are shown for Navidrome tracks";
        case LyricsPanelStatus::NotConfigured:  return "Navidrome server not configured";
        case LyricsPanelStatus::Loading:        return "Loading lyrics…";
        case LyricsPanelStatus::Failed:         return "Couldn't load lyrics (see console log)";
        case LyricsPanelStatus::NoLyrics:       return "No lyrics on the server for this track";
        case LyricsPanelStatus::Showing:        return "";
    }
    return "";
}

inline int lyricsScrollToCenter(int lineTop, int lineHeight, int viewHeight, int contentHeight) {
    const int target = lineTop + lineHeight / 2 - viewHeight / 2;
    return (std::max)(0, (std::min)(target, (std::max)(0, contentHeight - viewHeight)));
}

}
