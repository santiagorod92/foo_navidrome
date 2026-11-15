#pragma once

#include <string>
#include <utility>
#include <vector>

namespace navidrome {

struct ReplayGain {
    double trackGain = 0.0;
    double albumGain = 0.0;
    double trackPeak = 0.0;
    double albumPeak = 0.0;
    bool   hasTrackGain = false;
    bool   hasAlbumGain = false;
    bool   hasTrackPeak = false;
    bool   hasAlbumPeak = false;

    bool empty() const {
        return !hasTrackGain && !hasAlbumGain && !hasTrackPeak && !hasAlbumPeak;
    }
};

inline ReplayGain sanitizeReplayGain(ReplayGain rg) {
    if (rg.hasTrackPeak && rg.trackPeak <= 0.0) rg.hasTrackPeak = false;
    if (rg.hasAlbumPeak && rg.albumPeak <= 0.0) rg.hasAlbumPeak = false;
    const bool gainsZero = (!rg.hasTrackGain || rg.trackGain == 0.0) &&
                           (!rg.hasAlbumGain || rg.albumGain == 0.0);
    if (gainsZero && !rg.hasTrackPeak && !rg.hasAlbumPeak) return ReplayGain{};
    return rg;
}

struct TrackTags {
    std::string              albumArtist;
    std::string              composer;
    std::vector<std::string> genres;
    std::string              musicBrainzId;
    int                      discNumber = 0;
    int                      bpm        = 0;
    int                      bitRate    = 0;
    int                      sampleRate = 0;
    int                      bitDepth   = 0;
    int                      channels   = 0;
    ReplayGain               replayGain;
};

constexpr const char* kMusicBrainzTrackIdTag = "MUSICBRAINZ_TRACKID";

inline std::vector<std::pair<std::string, std::string>> trackTagMetaFields(const TrackTags& t) {
    std::vector<std::pair<std::string, std::string>> out;
    if (!t.albumArtist.empty())   out.emplace_back("album artist", t.albumArtist);
    if (!t.composer.empty())      out.emplace_back("composer", t.composer);
    for (const auto& g : t.genres)
        if (!g.empty())           out.emplace_back("genre", g);
    if (t.discNumber > 0)         out.emplace_back("discnumber", std::to_string(t.discNumber));
    if (t.bpm > 0)                out.emplace_back("bpm", std::to_string(t.bpm));
    if (!t.musicBrainzId.empty()) out.emplace_back(kMusicBrainzTrackIdTag, t.musicBrainzId);
    return out;
}

inline std::vector<std::pair<std::string, std::string>> trackTechInfoFields(const TrackTags& t) {
    std::vector<std::pair<std::string, std::string>> out;
    if (t.bitRate > 0)    out.emplace_back("bitrate", std::to_string(t.bitRate));
    if (t.sampleRate > 0) out.emplace_back("samplerate", std::to_string(t.sampleRate));
    if (t.bitDepth > 0)   out.emplace_back("bitspersample", std::to_string(t.bitDepth));
    if (t.channels > 0)   out.emplace_back("channels", std::to_string(t.channels));
    return out;
}
}
