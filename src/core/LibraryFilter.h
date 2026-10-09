#pragma once

#include "SubsonicModels.h"
#include <algorithm>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace navidrome {

inline std::vector<std::string> parseMusicFolderIds(const std::string& csv) {
    std::vector<std::string> out;
    std::string cur;
    auto flush = [&]() {
        const char* ws = " \t\r\n";
        size_t b = cur.find_first_not_of(ws);
        size_t e = cur.find_last_not_of(ws);
        if (b != std::string::npos) {
            std::string id = cur.substr(b, e - b + 1);
            if (std::find(out.begin(), out.end(), id) == out.end())
                out.push_back(id);
        }
        cur.clear();
    };
    for (char ch : csv) {
        if (ch == ',') flush();
        else           cur.push_back(ch);
    }
    flush();
    return out;
}

inline std::string joinMusicFolderIds(const std::vector<std::string>& ids) {
    std::string out;
    for (const auto& id : ids) {
        if (!out.empty()) out += ',';
        out += id;
    }
    return out;
}

inline std::vector<std::string> effectiveMusicFolderIds(
        bool filterEnabled,
        const std::string& selectedCsv,
        const std::vector<MusicFolder>& serverFolders) {
    if (!filterEnabled)              return {};
    if (serverFolders.size() < 2)   return {};

    std::vector<std::string> selected = parseMusicFolderIds(selectedCsv);
    if (selected.empty())           return {};

    std::vector<std::string> result;
    for (const auto& f : serverFolders) {
        if (std::find(selected.begin(), selected.end(), f.id) != selected.end())
            result.push_back(f.id);
    }
    if (result.empty())                       return {};
    if (result.size() == serverFolders.size()) return {};
    return result;
}

inline std::string appendMusicFolderParam(std::string params,
                                          const std::string& folderId) {
    if (folderId.empty()) return params;
    if (!params.empty()) params += '&';
    params += "musicFolderId=" + folderId;
    return params;
}

template <class T, class Fetch, class IdOf>
inline std::vector<T> mergeFanOut(const std::vector<std::string>& folderIds,
                                  Fetch fetch, IdOf idOf) {
    if (folderIds.empty()) return fetch(std::string());
    std::vector<T> merged;
    std::unordered_set<std::string> seen;
    for (const auto& fid : folderIds) {
        std::vector<T> part = fetch(fid);
        for (auto& item : part) {
            std::string id = idOf(item);
            if (id.empty() || seen.insert(id).second)
                merged.push_back(std::move(item));
        }
    }
    return merged;
}

inline std::vector<Album> filterAlbumsByArtistSearch(
        const std::vector<Album>& artistAlbums,
        const std::string& artistId,
        const std::vector<Album>& searchAlbums,
        bool& outUnconfirmed) {
    outUnconfirmed = false;
    std::vector<std::string> allowed;
    for (const auto& a : searchAlbums)
        if (!a.id.empty() && a.artistId == artistId)
            allowed.push_back(a.id);
    if (allowed.empty()) { outUnconfirmed = true; return artistAlbums; }

    std::vector<Album> filtered;
    for (const auto& a : artistAlbums)
        if (std::find(allowed.begin(), allowed.end(), a.id) != allowed.end())
            filtered.push_back(a);
    return filtered;
}
}
