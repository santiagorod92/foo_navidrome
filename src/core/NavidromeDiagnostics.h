#pragma once
#include "NavidromeDebugLog.h"
#include "SubsonicTypes.h"

#include <string>
#include <vector>

namespace navidrome {

struct DiagnosticsInfo {
    std::string componentVersion;
    std::string foobarVersion;
    std::string platform;
    std::string osVersion;
    std::string arch;
    std::string wineVersion;

    bool        configured = false;
    std::string serverUrl;
    bool        serverReached = false;
    ServerInfo  server;
    std::string serverError;

    std::string transcodeFormat;
    int         maxBitrate = 0;
    bool        scrobble = false;
    bool        startupRefresh = false;
    bool        customHeaders = false;
    bool        libraryFilter = false;
    std::size_t libraryCount = 0;
    bool        audioMuse = false;
    bool        verboseLogging = false;

    std::vector<std::string> logLines;
};

inline std::string urlHost(const std::string& url) {
    size_t start = url.find("://");
    start = (start == std::string::npos) ? 0 : start + 3;
    const size_t at = url.find('@', start);
    const size_t end0 = url.find_first_of("/?#", start);
    if (at != std::string::npos && (end0 == std::string::npos || at < end0)) start = at + 1;
    size_t end = url.find_first_of(":/?#", start);
    if (end == std::string::npos) end = url.size();
    return url.substr(start, end - start);
}

inline std::string redactHost(std::string text, const std::string& host) {
    if (host.empty()) return text;
    auto lower = [](std::string s) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    const std::string needle = lower(host);
    std::string hay = lower(text);
    size_t pos = 0;
    while ((pos = hay.find(needle, pos)) != std::string::npos) {
        text.replace(pos, needle.size(), "<server>");
        hay.replace(pos, needle.size(), "<server>");
        pos += 8;
    }
    return text;
}

std::string collectDiagnostics();
std::string componentLogPath();

inline std::string buildDiagnostics(const DiagnosticsInfo& d) {
    const std::string host = urlHost(d.serverUrl);
    auto clean = [&](const std::string& s) { return redactHost(dbg::scrubAuth(s), host); };
    auto yesNo = [](bool b) { return std::string(b ? "on" : "off"); };

    std::string out;
    out += "### foo_navidrome diagnostics\n\n";
    out += "- Component: " + d.componentVersion + "\n";
    out += "- foobar2000: " + d.foobarVersion + "\n";
    out += "- OS: " + d.platform + " " + d.osVersion + " (" + d.arch + ")";
    if (!d.wineVersion.empty()) out += ", Wine " + d.wineVersion;
    out += "\n";

    if (!d.configured) {
        out += "- Server: not configured\n";
    } else {
        out += "- Server URL: " + clean(d.serverUrl) + "\n";
        if (d.serverReached) {
            out += "- Server: " + describeServer(d.server) + "\n";
            if (d.server.openSubsonic)
                out += "- OpenSubsonic extensions: " + describeExtensions(d.server) + "\n";
        } else {
            out += "- Server: unreachable (" + clean(d.serverError.empty() ? "unknown error"
                                                                             : d.serverError) + ")\n";
        }
    }

    out += "- Stream as: " + (d.transcodeFormat.empty() ? std::string("server default")
                                                         : d.transcodeFormat);
    out += ", max bitrate: " + (d.maxBitrate > 0 ? std::to_string(d.maxBitrate) + " kbps"
                                                  : std::string("unlimited")) + "\n";
    out += "- Scrobbling: " + yesNo(d.scrobble) +
           ", startup rating refresh: " + yesNo(d.startupRefresh) +
           ", custom headers: " + (d.customHeaders ? "set" : "none") + "\n";
    out += "- Library filter: " + (d.libraryFilter ? std::to_string(d.libraryCount) + " selected"
                                                   : std::string("off")) +
           ", AudioMuse-AI: " + (d.audioMuse ? "configured" : "not configured") + "\n";
    out += "- Verbose logging: " + yesNo(d.verboseLogging) + "\n";

    out += "\n<details><summary>Recent log (" + std::to_string(d.logLines.size()) +
           " lines)</summary>\n\n```\n";
    for (const auto& l : d.logLines) out += clean(l) + "\n";
    out += "```\n</details>\n";
    return out;
}
}
