#include "TestHarness.h"
#include "../src/core/NavidromeDiagnostics.h"

#include <string>
#include <vector>

namespace {

bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

TEST_CASE(testScrubAuth) {
    using navidrome::dbg::scrubAuth;
    const std::string stream = scrubAuth(
        "https://music.example.com/rest/stream.view?id=42&u=alice&t=abc123&s=salty&v=1.16.1&c=fb2k");
    check(!contains(stream, "alice") && !contains(stream, "abc123") && !contains(stream, "salty"),
          "scrubAuth hides user, token and salt in a stream URL");
    check(contains(stream, "id=42") && contains(stream, "v=1.16.1") && contains(stream, "stream.view"),
          "scrubAuth keeps the endpoint and the other params");
    check(!contains(scrubAuth("https://h/rest/ping.view?p=enc:6869&u=bob"), "6869"),
          "scrubAuth hides a legacy p= password");
    check(!contains(scrubAuth("https://h/rest/ping.view?apiKey=SECRETKEY&f=json"), "SECRETKEY"),
          "scrubAuth hides an OpenSubsonic apiKey");
    check(!contains(scrubAuth("https://bob:hunter2@music.example.com/rest/x"), "hunter2"),
          "scrubAuth hides URL userinfo credentials");
    check(scrubAuth("https://h/rest/search3.view?query=artist&songCount=5") ==
              "https://h/rest/search3.view?query=artist&songCount=5",
          "scrubAuth leaves params that only end in t=/s= alone");
    check(!contains(scrubAuth("GET https://h/rest/x?u=alice&t=tok  (retry 2)"), "tok "),
          "scrubAuth stops a value at whitespace inside a log message");
}

TEST_CASE(testLogLineAndRing) {
    using namespace navidrome::dbg;
    const LocalTime t{ 2026, 10, 9, 7, 5, 3, 42 };
    check(formatLine(t, "WARN", "HTTP", 12, "boom") == "07:05:03.042  WARN   HTTP      [t0012] boom",
          "formatLine keeps the column layout the log scripts parse");
    check(formatDate(t) == "2026-10-09", "formatDate is ISO");

    LineRing ring(3);
    for (const char* s : { "a", "b", "c", "d" }) ring.push(s);
    const auto lines = ring.lines();
    check(lines.size() == 3 && lines[0] == "b" && lines[2] == "d",
          "LineRing keeps only the newest lines, oldest first");
    LineRing none(0);
    none.push("x");
    check(none.lines().empty(), "a zero-capacity ring keeps nothing");
}

TEST_CASE(testServerInfoParse) {
    std::string err;
    auto root = navidrome::json::parse(
        R"j({"subsonic-response":{"status":"ok","version":"1.16.1","type":"navidrome",)j"
        R"j("serverVersion":"0.55.2 (abc)","openSubsonic":true}})j", err);
    const auto info = navidrome::parseServerInfo(root["subsonic-response"]);
    check(err.empty() && info.apiVersion == "1.16.1" && info.type == "navidrome" &&
          info.serverVersion == "0.55.2 (abc)" && info.openSubsonic,
          "parseServerInfo reads the OpenSubsonic ping fields");

    auto plain = navidrome::json::parse(R"({"subsonic-response":{"status":"ok","version":"1.15.0"}})", err);
    const auto p = navidrome::parseServerInfo(plain["subsonic-response"]);
    check(p.apiVersion == "1.15.0" && p.type.empty() && !p.openSubsonic,
          "a plain Subsonic ping leaves the OpenSubsonic fields empty");
    check(p.extensionsKnown && p.lacksExtension("songLyrics") && !info.extensionsKnown &&
              !info.lacksExtension("songLyrics"),
          "plain Subsonic: no extensions, known; OpenSubsonic: unknown until fetched");

    auto ext = navidrome::json::parse(
        R"j({"subsonic-response":{"status":"ok","openSubsonicExtensions":[)j"
        R"j({"name":"songLyrics","versions":[1]},{"name":"transcodeOffset","versions":[1,2]},)j"
        R"j({"versions":[1]}]}})j", err);
    auto withExt = info;
    withExt.extensions      = navidrome::parseOpenSubsonicExtensions(ext["subsonic-response"]);
    withExt.extensionsKnown = true;
    check(withExt.extensions.size() == 2 && withExt.hasExtension("transcodeOffset") &&
              !withExt.lacksExtension("songLyrics") && withExt.lacksExtension("apiKeyAuthentication"),
          "extension list parsed, nameless entries skipped");
    check(navidrome::describeExtensions(withExt) == "songLyrics v1, transcodeOffset v1/v2" &&
              navidrome::describeExtensions(info) == "unknown" &&
              navidrome::describeExtensions(p) == "none",
          "describeExtensions formats names + versions, or unknown/none");
}

TEST_CASE(testDiagnosticsText) {
    using navidrome::urlHost;
    check(urlHost("https://Music.Example.com:4533/navidrome/") == "Music.Example.com",
          "urlHost drops scheme, port and path");
    check(urlHost("http://bob:pw@10.0.0.5/") == "10.0.0.5", "urlHost skips userinfo");
    check(navidrome::redactHost("GET https://MUSIC.example.com/rest", "music.example.com") ==
              "GET https://<server>/rest",
          "redactHost is case-insensitive");

    navidrome::DiagnosticsInfo d;
    d.componentVersion = "1.30.0";
    d.foobarVersion    = "foobar2000 v2.25";
    d.platform         = "Windows";
    d.osVersion        = "10.0.26100";
    d.arch             = "x64";
    d.configured       = true;
    d.serverUrl        = "https://music.example.com:4533/";
    d.serverReached    = true;
    d.server.apiVersion    = "1.16.1";
    d.server.type          = "navidrome";
    d.server.serverVersion = "0.55.2";
    d.server.openSubsonic  = true;
    d.customHeaders    = true;
    d.logLines         = { "10:00:00.000  ERROR  HTTP      [t0001] GET https://music.example.com/rest/"
                           "getAlbum.view?id=7&u=alice&t=tok123 failed" };

    const std::string text = navidrome::buildDiagnostics(d);
    check(contains(text, "1.30.0") && contains(text, "foobar2000 v2.25") &&
          contains(text, "Windows 10.0.26100 (x64)"),
          "diagnostics carry component, foobar and OS versions");
    check(contains(text, "navidrome 0.55.2, API 1.16.1, OpenSubsonic"),
          "diagnostics carry what the server says about itself");
    check(contains(text, "OpenSubsonic extensions: unknown"),
          "an OpenSubsonic server whose extension list wasn't read says so");
    d.server.extensions      = { { "songLyrics", { 1 } } };
    d.server.extensionsKnown = true;
    check(contains(navidrome::buildDiagnostics(d), "OpenSubsonic extensions: songLyrics v1"),
          "the extension list is in the diagnostics");
    check(!contains(text, "music.example.com") && contains(text, "https://<server>:4533/"),
          "the server host is redacted everywhere, scheme and port kept");
    check(!contains(text, "alice") && !contains(text, "tok123") && contains(text, "getAlbum.view?id=7"),
          "log lines are scrubbed again, endpoint kept");
    check(contains(text, "custom headers: set") && contains(text, "Recent log (1 lines)"),
          "custom headers are reported as set/none only; the log tail is included");

    d.serverReached = false;
    d.serverError   = "Could not connect to https://music.example.com";
    check(contains(navidrome::buildDiagnostics(d), "unreachable (Could not connect to https://<server>)"),
          "an unreachable server reports the error, host redacted");

    navidrome::DiagnosticsInfo blank;
    blank.platform = "macOS";
    check(contains(navidrome::buildDiagnostics(blank), "Server: not configured"),
          "an unconfigured component says so");
}
}
