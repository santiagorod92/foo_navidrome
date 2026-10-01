// Unit tests: MediaEnrichmentLogic.h/.cpp — URL/URI codec, cover-art URL, HTTP
// classification, the LRU CoverCache, ESLyric config generation and MD5.
#include "TestHarness.h"
#include "../src/core/MediaEnrichmentLogic.h"
#include "../src/core/SubsonicTypes.h"

#include <string>
#include <vector>

namespace {

TEST_CASE(testUriEncodeDecode) {
    using navidrome::uriEncode;
    using navidrome::uriDecode;
    check(uriEncode("abc-_.~XYZ019") == "abc-_.~XYZ019",
        "unreserved characters pass through unescaped");
    check(uriEncode(" a/b?c&d") == "%20a%2Fb%3Fc%26d",
        "reserved and space characters are percent-encoded");
    check(uriEncode("café") == "caf%C3%A9",
        "UTF-8 bytes are individually percent-encoded");
    check(uriDecode("a%20b%2Fc") == "a b/c", "decode reverses encode");
    check(uriDecode("a%2fb") == "a/b", "lowercase hex escapes decode too");
    check(uriEncode("a+b c") == "a%2Bb%20c",
        "'+' and space are both percent-encoded (not form-encoding)");
    check(uriDecode("100%") == "100%",
        "a trailing bare percent is passed through, not dropped");
    check(uriDecode("50%2 off") == "50%2 off",
        "an incomplete escape (non-hex second digit) is passed through");
    check(uriDecode(uriEncode("中文 + spaces & symbols")) ==
        "中文 + spaces & symbols", "round-trip preserves arbitrary text");
}

TEST_CASE(testNormalizeUrl) {
    using navidrome::normalizeMediaServerUrl;
    check(normalizeMediaServerUrl("  https://Example.COM/Root/Path/  ") ==
        "https://example.com/Root/Path",
        "scheme+host lowercased, path case preserved, trailing slash/space trimmed");
    check(normalizeMediaServerUrl("HTTP://HOST") == "http://host",
        "bare host with no path is fully lowercased");
    check(normalizeMediaServerUrl("not-a-url") == "not-a-url",
        "a value with no scheme separator is left alone (minus trim)");
    check(normalizeMediaServerUrl("") == "", "empty input stays empty");
    check(normalizeMediaServerUrl("\thttps://Host/Keep/Case\t") ==
        "https://host/Keep/Case",
        "leading/trailing tabs are trimmed, host lowercased, path case kept");
    check(normalizeMediaServerUrl("HTTPS://Host:8080/Path") ==
        "https://host:8080/Path",
        "an explicit port is preserved and lowercased with the host, not the path");
    check(normalizeMediaServerUrl("https://host///") == "https://host",
        "every trailing slash is trimmed, not just one");
}

TEST_CASE(testJsEscapeEdgeCases) {
    using navidrome::jsEscape;
    check(jsEscape("a\"b\\c") == "a\\\"b\\\\c", "quote and backslash are escaped");
    check(jsEscape("a\tb\nc\rd") == "a\\tb\\nc\\rd",
        "tab/newline/carriage-return use short escapes");
    check(jsEscape(std::string(1, '\x01')) == "\\u0001",
        "other control characters use \\u escapes");
    check(jsEscape("") == "", "empty input stays empty");
    check(jsEscape("emoji 😀 survives") == "emoji 😀 survives",
        "non-control UTF-8 bytes pass through unescaped");
}

TEST_CASE(testIdentifiers) {
    using navidrome::resolveArtId;
    check(resolveArtId("navidrome://track/song?coverArt=cover%2Fone&id=ignored") ==
        "cover/one", "coverArt has priority and decodes once");
    check(resolveArtId("navidrome://track/song%252Fraw") == "song%2Fraw",
        "path id is decoded exactly once");
    check(resolveArtId("navidrome://track/%E4%B8%AD%E6%96%87%2Bplus+literal") ==
        "中文+plus+literal", "UTF-8, encoded plus and literal plus survive");
    check(resolveArtId("https://server/rest/stream.view?id=old%2Fid&u=user") ==
        "old/id", "legacy stream id is supported");
    check(resolveArtId("https://server/music.mp3").empty(), "unowned path has no id");

    using navidrome::isNavidromeArtPath;
    check(isNavidromeArtPath("navidrome://track/song?id=1"),
        "isNavidromeArtPath matches the navidrome:// scheme");
    check(isNavidromeArtPath("https://server/rest/stream.view?id=1"),
        "isNavidromeArtPath matches legacy stream.view URLs");
    check(!isNavidromeArtPath("https://server/music.mp3"),
        "isNavidromeArtPath rejects an unrelated URL");
    check(!isNavidromeArtPath(nullptr), "isNavidromeArtPath rejects a null path");
}

TEST_CASE(testCoverUrl) {
    const auto url = navidrome::buildCoverArtUrl(" HTTPS://Example.COM/root/ ",
        "user name", "distinct-password-9", "salt-42", "封面/id+", 300);
    check(url.find("https://example.com/root/rest/getCoverArt.view?") == 0,
        "server identity is normalized");
    check(url.find("u=user%20name") != std::string::npos, "username is encoded");
    check(url.find("t=404424f3a47ba68fb27a01d8c4eea719") != std::string::npos,
        "MD5 token matches known vector");
    check(url.find("s=salt-42") != std::string::npos, "salt is present");
    check(url.find("id=%E5%B0%81%E9%9D%A2%2Fid%2B") != std::string::npos,
        "cover id is encoded exactly once");
    check(url.find("size=300") != std::string::npos, "requested size is present");
    check(url.find("distinct-password-9") == std::string::npos,
        "raw password is absent from URL");

    const auto noSize = navidrome::buildCoverArtUrl("https://s", "u", "p",
        "salt", "cid", 0);
    check(noSize.find("size=") == std::string::npos,
        "size is omitted entirely when not positive");
    check(noSize.find("v=1.16.1") != std::string::npos &&
          noSize.find("c=foo_navidrome") != std::string::npos &&
          noSize.find("f=json") != std::string::npos,
        "the fixed Subsonic client params are always present");
}

TEST_CASE(testClassification) {
    using navidrome::FetchClass;
    using navidrome::classifyBody;
    using navidrome::classifyHttpStatus;

    check(classifyHttpStatus(200) == FetchClass::Ok, "HTTP 200");
    check(classifyHttpStatus(401) == FetchClass::Auth, "HTTP 401");
    check(classifyHttpStatus(403) == FetchClass::Auth, "HTTP 403");
    check(classifyHttpStatus(404) == FetchClass::NotFound, "HTTP 404");
    check(classifyHttpStatus(410) == FetchClass::NotFound, "HTTP 410 Gone");
    check(classifyHttpStatus(500) == FetchClass::ServerError, "HTTP 500 lower bound");
    check(classifyHttpStatus(503) == FetchClass::ServerError, "HTTP 5xx");
    check(classifyHttpStatus(599) == FetchClass::ServerError, "HTTP 599 upper bound");
    check(classifyHttpStatus(600) == FetchClass::Transport,
        "a status past the 5xx range is a transport failure");
    check(classifyHttpStatus(302) == FetchClass::Transport,
        "an unmapped status (redirect) is treated as a transport failure");
    check(classifyHttpStatus(0) == FetchClass::Transport,
        "a zero status (no response line) is a transport failure");

    check(classifyBody("image/jpeg", {0xff, 0xd8, 0xff, 0x00}) == FetchClass::Ok,
        "JPEG magic");
    check(classifyBody("application/octet-stream",
        {0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a}) == FetchClass::Ok,
        "PNG magic");
    check(classifyBody("image/avif", bytes("unknown-format")) == FetchClass::Ok,
        "image MIME fallback");
    check(classifyBody("text/html", bytes("<html>error</html>")) ==
        FetchClass::InvalidContent, "non-image content");
    check(classifyBody("image/jpeg", bytes("12345"), 4) ==
        FetchClass::InvalidContent, "body above limit");
    check(classifyBody("application/json", bytes(
        R"({"subsonic-response":{"status":"failed","error":{"code":70}}})")) ==
        FetchClass::NotFound, "Subsonic JSON not-found");
    check(classifyBody("application/json", bytes(
        R"({"subsonic-response":{"status":"failed","error":{"code":40}}})")) ==
        FetchClass::Auth, "Subsonic JSON auth");
    check(classifyBody("text/xml", bytes(
        R"(<subsonic-response status="failed"><error code="44"/></subsonic-response>)")) ==
        FetchClass::Auth, "Subsonic XML auth");
    check(classifyBody("application/json", bytes(
        R"({"subsonic-response":{"status":"failed","error":{"code":10}}})")) ==
        FetchClass::ServerError, "other Subsonic error");

    check(classifyBody("application/octet-stream", {'G', 'I', 'F', '8', '9', 'a'}) ==
        FetchClass::Ok, "GIF magic");
    check(classifyBody("application/octet-stream", {'B', 'M', 0x00, 0x00}) ==
        FetchClass::Ok, "BMP magic");
    check(classifyBody("application/octet-stream",
        {'R', 'I', 'F', 'F', 0x00, 0x00, 0x00, 0x00, 'W', 'E', 'B', 'P'}) ==
        FetchClass::Ok, "WEBP RIFF container magic");
    check(classifyBody("image/jpeg", {}) == FetchClass::InvalidContent,
        "an empty body is never valid content");
    check(classifyBody("IMAGE/JPEG", bytes("not really an image")) == FetchClass::Ok,
        "the image/ content-type check is case-insensitive");
    check(classifyBody("image/jpeg", {0xff, 0xd8, 0xff}, 3) == FetchClass::Ok,
        "a body exactly at the size limit is still inspected (limit is exclusive)");
    check(classifyBody("image/jpeg", bytes(
        R"({"subsonic-response":{"status":"failed","error":{"code":70}}})")) ==
        FetchClass::NotFound,
        "a Subsonic error wins even when the content-type claims an image");
    check(classifyBody("application/json", bytes(
        R"({"subsonic-response":{"status":"failed","error":{"code":41}}})")) ==
        FetchClass::Auth, "Subsonic error code 41 is an auth failure");
    check(classifyBody("application/json", bytes(
        R"({"subsonic_response":{"status":"failed","error":{"code":70}}})")) ==
        FetchClass::NotFound,
        "the subsonic_response underscore spelling is also recognised");
    check(classifyBody("application/json", bytes(
        R"({"subsonic-response":{"status":"failed","error":{"code":0}}})")) ==
        FetchClass::InvalidContent,
        "error code 0 is not a positive code, so body inspection continues");
    check(classifyBody("application/json", bytes(
        R"({"subsonic-response":{"status":"failed"}})")) ==
        FetchClass::InvalidContent,
        "a failed response with no code field falls through to content inspection");
}

TEST_CASE(testCache) {
    auto& cache = navidrome::CoverCache::instance();
    cache.clear();
    cache.put("HTTPS://EXAMPLE.COM/", "alice", "cover", {1, 2, 3});
    check(cache.get("https://example.com", "alice", "cover") ==
        std::vector<std::uint8_t>({1, 2, 3}), "cache normalizes server identity");
    check(cache.get("https://example.com", "bob", "cover").empty(),
        "cache separates users");
    cache.put("https://identity", "user\npart", "cover", {7});
    cache.put("https://identity", "user", "part\ncover", {8});
    check(cache.get("https://identity", "user\npart", "cover") ==
        std::vector<std::uint8_t>({7}), "cache key frames username field");
    check(cache.get("https://identity", "user", "part\ncover") ==
        std::vector<std::uint8_t>({8}), "cache key frames cover-id field");

    cache.clear();
    for (int index = 0; index < 32; ++index) {
        cache.put("https://server", "user", "cover-" + std::to_string(index),
            {static_cast<std::uint8_t>(index)});
    }
    check(!cache.get("https://server", "user", "cover-0").empty(),
        "cache hit refreshes LRU order");
    cache.put("https://server", "user", "cover-32", {32});
    check(cache.get("https://server", "user", "cover-1").empty(),
        "least recently used entry is evicted");
    check(!cache.get("https://server", "user", "cover-0").empty(),
        "recently touched entry survives eviction");

    // Overwriting an existing key must adjust the byte counter down by the old
    // size before adding the new one — an underflow there would make every
    // later put() think the cache is over budget and evict spuriously.
    cache.clear();
    cache.put("https://s", "u", "k", {1, 2, 3});
    cache.put("https://s", "u", "k", {9});
    check(cache.get("https://s", "u", "k") == std::vector<std::uint8_t>({9}),
        "overwriting a cache key replaces its bytes");
    cache.put("https://s", "u", "k2", {5});
    check(!cache.get("https://s", "u", "k").empty(),
        "an overwrite keeps the byte counter sane (no spurious eviction)");

    // Byte-total eviction (48 MiB budget) is a separate path from the 32-entry
    // cap and otherwise has no coverage. The literal mirrors CoverCache::kMaxBytes.
    cache.clear();
    const std::size_t kMaxBytes = 48u * 1024u * 1024u;
    cache.put("https://s", "u", "big", std::vector<std::uint8_t>(kMaxBytes, 1));
    check(!cache.get("https://s", "u", "big").empty(),
        "an entry exactly at the byte budget is accepted");
    cache.put("https://s", "u", "small", {7});
    check(cache.get("https://s", "u", "big").empty(),
        "exceeding the byte budget evicts the least recently used entry");
    check(cache.get("https://s", "u", "small") == std::vector<std::uint8_t>({7}),
        "the entry that pushed past the budget stays");
    cache.clear();
}

TEST_CASE(testConfig) {
    const std::string password = "distinct-password-9";
    const auto config = navidrome::buildEsLyricConfigJs(
        " HTTPS://Example.COM/root/ ", "user\"name", password, "salt-42",
        {{"X-Access", "line1\r\nline2"}, {"中文", "值😀"}}, "1.3.0");
    check(config.find("export const config") != std::string::npos,
        "config module exports canonical object");
    check(config.find("https://example.com/root") != std::string::npos,
        "config normalizes server URL");
    check(config.find("404424f3a47ba68fb27a01d8c4eea719") != std::string::npos,
        "config derives known token");
    check(config.find(password) == std::string::npos,
        "config never contains raw password");
    check(config.find("user\\\"name") != std::string::npos,
        "config escapes quotes");
    check(config.find("line1\\r\\nline2") != std::string::npos,
        "config escapes line breaks");
    check(config.find("debug: false") != std::string::npos,
        "config defaults to quiet mode");
    check(config.find("componentVersion: \"1.3.0\"") != std::string::npos,
        "config exposes the caller's componentVersion (no hardcoded script version)");
    check(config == navidrome::buildEsLyricConfigJs(
        " HTTPS://Example.COM/root/ ", "user\"name", password, "salt-42",
        {{"X-Access", "line1\r\nline2"}, {"中文", "值😀"}}, "1.3.0"),
        "config generation is stable");

    const auto withDebug = navidrome::buildEsLyricConfigJs(
        "https://s", "u", "p", "salt", {}, "2.0.0", true);
    check(withDebug.find("debug: true") != std::string::npos,
        "the debug flag is honoured when set");
    check(withDebug.find("headers: {}") != std::string::npos,
        "an empty header list renders as an empty object");
}

TEST_CASE(testMd5KnownAnswers) {
    // The MD5 primitive is the module's one platform-specific line (WinCrypt vs
    // CommonCrypto). A known-answer test on the empty string is a cheap canary
    // for that primitive being mis-wired on a new toolchain.
    const auto emptyToken = navidrome::buildCoverArtUrl(
        "https://s", "u", "", "", "cid", 0);
    check(emptyToken.find("t=d41d8cd98f00b204e9800998ecf8427e") != std::string::npos,
        "md5(\"\") matches the well-known digest");

    // Same credentials through buildCoverArtUrl and buildEsLyricConfigJs must
    // yield an identical token — both are md5(password + salt).
    const auto url = navidrome::buildCoverArtUrl("https://s", "u", "pw", "st", "cid", 0);
    const auto at = url.find("&t=") + 3;
    const auto token = url.substr(at, url.find('&', at) - at);
    check(token.size() == 32, "an MD5 hex digest is 32 characters");
    const auto cfg = navidrome::buildEsLyricConfigJs(
        "https://s", "u", "pw", "st", {}, "1.0.0");
    check(cfg.find("token: \"" + token + "\"") != std::string::npos,
        "cover-art URL and ESLyric config derive the same token from the same creds");
}

TEST_CASE(testCrossParserParity) {
    using navidrome::resolveArtId;
    using navidrome::trackIdFromURI;
    // resolveArtId (art extractor) and trackIdFromURI (scrobbler) are separate
    // implementations that both pull <id> out of navidrome://track/<id>.
    // CLAUDE.md flags scheme drift between the two as a live trap — pin them to
    // the same decoded output for ids that exercise the decoder.
    const char* ids[] = {"plain", "a/b", "a%2Fb", "中文+plus", "x?y"};
    for (const char* id : ids) {
        const auto uri = "navidrome://track/" + navidrome::uriEncode(id);
        check(resolveArtId(uri) == trackIdFromURI(uri),
            "resolveArtId and trackIdFromURI agree on the decoded id");
        check(resolveArtId(uri) == std::string(id),
            "the round-tripped id decodes back to the original");
    }
    const auto withQuery =
        "navidrome://track/" + navidrome::uriEncode("a/b") + "?rating=3&x=1";
    check(resolveArtId(withQuery) == "a/b" && trackIdFromURI(withQuery) == "a/b",
        "a trailing query string is stripped by both parsers before decoding");

    // resolveArtId's id= query branch and queryParamFromURI are two more query
    // parsers that must decode a parameter the same way.
    check(resolveArtId("navidrome://track/ignored?id=a%2Fb") ==
          navidrome::queryParamFromURI("navidrome://track/ignored?id=a%2Fb", "id"),
        "the id= query branch decodes the same as queryParamFromURI");
}

} // namespace
