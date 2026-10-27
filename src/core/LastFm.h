#pragma once
#include "SubsonicTypes.h"

#include <functional>
#include <string>
#include <vector>

namespace navidrome::lastfm {

inline std::string trimSpaces(const std::string& s) {
    std::size_t b = s.find_first_not_of(" \t");
    if (b == std::string::npos) return {};
    std::size_t e = s.find_last_not_of(" \t");
    return s.substr(b, e - b + 1);
}

inline std::vector<std::string> artistNameCandidates(const std::string& name) {
    std::vector<std::string> out;
    const std::string whole = trimSpaces(name);
    if (whole.empty()) return out;
    out.push_back(whole);
    const std::size_t comma = whole.find(',');
    if (comma == std::string::npos || whole.find(',', comma + 1) != std::string::npos) return out;
    const std::string last  = trimSpaces(whole.substr(0, comma));
    const std::string first = trimSpaces(whole.substr(comma + 1));
    if (!last.empty() && !first.empty()) out.push_back(first + " " + last);
    return out;
}

inline std::string artistInfoURL(const std::string& artist, const std::string& apiKey,
                                 const std::string& lang) {
    std::string url = "https://ws.audioscrobbler.com/2.0/?method=artist.getinfo&artist=" +
                      percentEncode(artist) + "&autocorrect=1";
    if (!lang.empty()) url += "&lang=" + percentEncode(lang);
    return url + "&api_key=" + percentEncode(apiKey) + "&format=json";
}

inline std::string cleanBiography(const std::string& html) {
    std::string bio = html;
    const std::size_t more = bio.find("<a href=\"https://www.last.fm/");
    if (more != std::string::npos) bio.erase(more);
    return stripHtmlTags(bio);
}

inline bool parseArtistInfo(const std::string& body, std::string& biography, std::string& url) {
    std::string err;
    const json::Value root = json::parse(body, err);
    const json::Value& artist = root["artist"];
    if (!artist.isObject()) return false;
    std::string bio = cleanBiography(jStr(artist["bio"], "summary"));
    if (bio.empty()) bio = cleanBiography(jStr(artist["bio"], "content"));
    if (bio.empty()) return false;
    biography = bio;
    url = jStr(artist, "url");
    return true;
}

using Fetch = std::function<std::string(const std::string& url)>;

inline ArtistInfo withBiographyFallback(ArtistInfo info, const std::string& artistName,
                                        const std::string& apiKey, const std::string& lang,
                                        const Fetch& fetch) {
    if (!stripHtmlTags(info.biography).empty() || apiKey.empty()) return info;
    for (const auto& candidate : artistNameCandidates(artistName)) {
        std::string bio, url;
        if (!parseArtistInfo(fetch(artistInfoURL(candidate, apiKey, lang)), bio, url)) continue;
        info.biography = bio;
        if (!url.empty()) info.lastFmUrl = url;
        return info;
    }
    return info;
}

}
