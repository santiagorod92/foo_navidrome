#pragma once

#include "TrackUri.h"

#include <cstddef>
#include <sstream>
#include <string>
#include <vector>

namespace navidrome {

constexpr const char* kDefaultServerUrl = "http://localhost:4533/";

struct ServerProfile {
    std::string name;
    std::string url;
    std::string username;
    std::string password;
    std::string customHeaders;
    bool        libraryFilter = false;
    std::string libraryIds;
};

struct ServerProfileState {
    std::vector<ServerProfile> profiles;
    std::string                active;
};

inline std::string formatServerProfiles(const std::vector<ServerProfile>& profiles) {
    std::string out;
    for (const auto& p : profiles) {
        out += "name=" + percentEncode(p.name) + "&url=" + percentEncode(p.url) +
               "&user=" + percentEncode(p.username) + "&pass=" + percentEncode(p.password) +
               "&headers=" + percentEncode(p.customHeaders) +
               "&filter=" + (p.libraryFilter ? "1" : "0") + "&libs=" + percentEncode(p.libraryIds) + "\n";
    }
    return out;
}

inline std::vector<ServerProfile> parseServerProfiles(const std::string& text) {
    std::vector<ServerProfile> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        ServerProfile p;
        std::size_t pos = 0;
        while (pos <= line.size()) {
            std::size_t amp = line.find('&', pos);
            if (amp == std::string::npos) amp = line.size();
            const std::string pair = line.substr(pos, amp - pos);
            const std::size_t eq = pair.find('=');
            if (eq != std::string::npos) {
                const std::string key = pair.substr(0, eq);
                const std::string value = percentDecode(pair.substr(eq + 1));
                if (key == "name")         p.name = value;
                else if (key == "url")     p.url = value;
                else if (key == "user")    p.username = value;
                else if (key == "pass")    p.password = value;
                else if (key == "headers") p.customHeaders = value;
                else if (key == "filter")  p.libraryFilter = value == "1";
                else if (key == "libs")    p.libraryIds = value;
            }
            pos = amp + 1;
        }
        if (!p.name.empty()) out.push_back(std::move(p));
    }
    return out;
}

inline int findServerProfile(const std::vector<ServerProfile>& profiles, const std::string& name) {
    for (std::size_t i = 0; i < profiles.size(); ++i)
        if (profiles[i].name == name) return static_cast<int>(i);
    return -1;
}

inline std::string serverProfileNameForUrl(const std::string& url) {
    std::string host = url;
    const std::size_t scheme = host.find("://");
    if (scheme != std::string::npos) host = host.substr(scheme + 3);
    const std::size_t at = host.find('@');
    if (at != std::string::npos) host = host.substr(at + 1);
    const std::size_t end = host.find_first_of("/:?#");
    if (end != std::string::npos) host = host.substr(0, end);
    return host.empty() ? std::string("Navidrome") : host;
}

inline std::string uniqueServerProfileName(const std::vector<ServerProfile>& profiles,
                                           const std::string& wanted) {
    const std::string base = wanted.empty() ? std::string("Navidrome") : wanted;
    if (findServerProfile(profiles, base) < 0) return base;
    for (int n = 2;; ++n) {
        const std::string candidate = base + " " + std::to_string(n);
        if (findServerProfile(profiles, candidate) < 0) return candidate;
    }
}

inline ServerProfileState syncActiveServerProfile(ServerProfileState state, const ServerProfile& activeFields) {
    int i = findServerProfile(state.profiles, state.active);
    if (i < 0) {
        ServerProfile p = activeFields;
        p.name = uniqueServerProfileName(state.profiles, state.active.empty()
                                             ? serverProfileNameForUrl(activeFields.url) : state.active);
        state.profiles.push_back(p);
        state.active = p.name;
        return state;
    }
    ServerProfile& p = state.profiles[static_cast<std::size_t>(i)];
    const std::string name = p.name;
    p = activeFields;
    p.name = name;
    return state;
}

inline bool switchServerProfile(ServerProfileState& state, const ServerProfile& activeFields,
                                const std::string& name, ServerProfile& load) {
    state = syncActiveServerProfile(std::move(state), activeFields);
    const int i = findServerProfile(state.profiles, name);
    if (i < 0 || name == state.active) return false;
    state.active = name;
    load = state.profiles[static_cast<std::size_t>(i)];
    return true;
}

inline ServerProfile addServerProfile(ServerProfileState& state, const ServerProfile& activeFields,
                                      const std::string& wantedName) {
    state = syncActiveServerProfile(std::move(state), activeFields);
    ServerProfile p;
    p.name = uniqueServerProfileName(state.profiles, wantedName);
    p.url  = kDefaultServerUrl;
    state.profiles.push_back(p);
    state.active = p.name;
    return p;
}

inline bool removeActiveServerProfile(ServerProfileState& state, const ServerProfile& activeFields,
                                      ServerProfile& load) {
    state = syncActiveServerProfile(std::move(state), activeFields);
    if (state.profiles.size() < 2) return false;
    const int i = findServerProfile(state.profiles, state.active);
    state.profiles.erase(state.profiles.begin() + i);
    load = state.profiles.front();
    state.active = load.name;
    return true;
}

}
