// MediaEnrichmentLogic.{h,cpp} still live under Windows/ (only the Windows
// component links them), but the module is SDK-free and its one platform-
// specific line is MD5 (#if defined(_WIN32) WinCrypt / #else CommonCrypto), so
// this host builds on Windows, Linux (make test) and macOS (make mac-test).
#include "../Windows/MediaEnrichmentLogic.h"
// SubsonicTypes.h is pure C++ (no SDK, no Windows headers), so its helpers can
// be exercised from this standalone host executable too.
#include "../SubsonicTypes.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* description) {
    if (condition) return;
    ++failures;
    std::cerr << "FAIL: " << description << '\n';
}

std::vector<std::uint8_t> bytes(const std::string& value) {
    return {value.begin(), value.end()};
}

void testUriEncodeDecode() {
    using navidrome::uriEncode;
    using navidrome::uriDecode;
    check(uriEncode("abc-_.~XYZ019") == "abc-_.~XYZ019",
        "unreserved characters pass through unescaped");
    check(uriEncode(" a/b?c&d") == "%20a%2Fb%3Fc%26d",
        "reserved and space characters are percent-encoded");
    check(uriEncode(u8"café") == "caf%C3%A9",
        "UTF-8 bytes are individually percent-encoded");
    check(uriDecode("a%20b%2Fc") == "a b/c", "decode reverses encode");
    check(uriDecode("a%2fb") == "a/b", "lowercase hex escapes decode too");
    check(uriEncode("a+b c") == "a%2Bb%20c",
        "'+' and space are both percent-encoded (not form-encoding)");
    check(uriDecode("100%") == "100%",
        "a trailing bare percent is passed through, not dropped");
    check(uriDecode("50%2 off") == "50%2 off",
        "an incomplete escape (non-hex second digit) is passed through");
    check(uriDecode(uriEncode(u8"中文 + spaces & symbols")) ==
        u8"中文 + spaces & symbols", "round-trip preserves arbitrary text");
}

void testNormalizeUrl() {
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

void testJsEscapeEdgeCases() {
    using navidrome::jsEscape;
    check(jsEscape("a\"b\\c") == "a\\\"b\\\\c", "quote and backslash are escaped");
    check(jsEscape("a\tb\nc\rd") == "a\\tb\\nc\\rd",
        "tab/newline/carriage-return use short escapes");
    check(jsEscape(std::string(1, '\x01')) == "\\u0001",
        "other control characters use \\u escapes");
    check(jsEscape("") == "", "empty input stays empty");
    check(jsEscape(u8"emoji 😀 survives") == u8"emoji 😀 survives",
        "non-control UTF-8 bytes pass through unescaped");
}

void testStarKindAndAlbumListType() {
    using navidrome::AlbumListType;
    using navidrome::StarKind;
    using navidrome::albumListTypeName;
    using navidrome::starParamName;

    check(std::string(starParamName(StarKind::Song)) == "id", "song stars use id=");
    check(std::string(starParamName(StarKind::Album)) == "albumId",
        "album stars use albumId=");
    check(std::string(starParamName(StarKind::Artist)) == "artistId",
        "artist stars use artistId=");

    check(std::string(albumListTypeName(AlbumListType::Newest)) == "newest",
        "newest is the default name");
    check(std::string(albumListTypeName(AlbumListType::Frequent)) == "frequent",
        "frequent list type name");
    check(std::string(albumListTypeName(AlbumListType::Recent)) == "recent",
        "recent list type name");
    check(std::string(albumListTypeName(AlbumListType::Random)) == "random",
        "random list type name");
    check(std::string(albumListTypeName(AlbumListType::Starred)) == "starred",
        "starred list type name");
}

void testParseHeaderLines() {
    using navidrome::parseHeaderLines;
    const auto lines = parseHeaderLines(
        "X-Access: token-one\r\n"
        "\n"
        "# a comment, skipped\n"
        "   \n"
        "  Y-Other: token-two  \n"
        "#also skipped");
    check(lines.size() == 2, "blank and comment lines are dropped");
    if (lines.size() == 2) {
        check(lines[0] == "X-Access: token-one",
            "CRLF is trimmed from a header line");
        check(lines[1] == "Y-Other: token-two",
            "surrounding whitespace is trimmed");
    }
    check(parseHeaderLines("").empty(), "empty blob yields no headers");
    check(parseHeaderLines("\n\n\n").empty(), "all-blank blob yields no headers");
    check(parseHeaderLines("no-trailing-newline: value").size() == 1,
        "a final line with no trailing newline is still captured");
    const auto more = parseHeaderLines("   # indented comment\nA: 1\nA: 2");
    check(more.size() == 2,
        "an indented '#' line is still treated as a comment and dropped");
    if (more.size() == 2) {
        check(more[0] == "A: 1" && more[1] == "A: 2",
            "duplicate header names are preserved in order (no dedup)");
    }
}

void testPercentDecodeEdgeCases() {
    using navidrome::percentDecode;
    check(percentDecode("a%2Fb") == "a/b", "a valid escape decodes");
    check(percentDecode("100%") == "100%",
        "a trailing bare percent with nothing after it is passed through");
    check(percentDecode("50%") == "50%",
        "a percent with fewer than two trailing characters is left as-is");
    check(percentDecode("bad%zzescape") == "bad%zzescape",
        "a non-hex escape is passed through unchanged");
    check(percentDecode("") == "", "empty input stays empty");
    check(percentDecode("a%2fb") == "a/b", "lowercase hex escapes decode");
    check(percentDecode("ab%2") == "ab%2",
        "an escape truncated at end of string is passed through, not consumed");
}

void testPlaylistChunkSize() {
    check(navidrome::kPlaylistChunkSize == 50,
        "playlist mutations are chunked at 50 ids per request");
}

void testIdentifiers() {
    using navidrome::resolveArtId;
    check(resolveArtId("navidrome://track/song?coverArt=cover%2Fone&id=ignored") ==
        "cover/one", "coverArt has priority and decodes once");
    check(resolveArtId("navidrome://track/song%252Fraw") == "song%2Fraw",
        "path id is decoded exactly once");
    check(resolveArtId("navidrome://track/%E4%B8%AD%E6%96%87%2Bplus+literal") ==
        u8"中文+plus+literal", "UTF-8, encoded plus and literal plus survive");
    check(resolveArtId("https://server/rest/stream.view?id=old%2Fid&u=user") ==
        "old/id", "legacy stream id is supported");
    check(resolveArtId("https://server/music.mp3").empty(), "unowned path has no id");
}

void testCoverUrl() {
    const auto url = navidrome::buildCoverArtUrl(" HTTPS://Example.COM/root/ ",
        "user name", "distinct-password-9", "salt-42", u8"封面/id+", 300);
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

void testClassification() {
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

void testCache() {
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

void testConfig() {
    const std::string password = "distinct-password-9";
    const auto config = navidrome::buildEsLyricConfigJs(
        " HTTPS://Example.COM/root/ ", "user\"name", password, "salt-42",
        {{"X-Access", "line1\r\nline2"}, {u8"中文", u8"值😀"}}, "1.3.0");
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
        {{"X-Access", "line1\r\nline2"}, {u8"中文", u8"值😀"}}, "1.3.0"),
        "config generation is stable");

    const auto withDebug = navidrome::buildEsLyricConfigJs(
        "https://s", "u", "p", "salt", {}, "2.0.0", true);
    check(withDebug.find("debug: true") != std::string::npos,
        "the debug flag is honoured when set");
    check(withDebug.find("headers: {}") != std::string::npos,
        "an empty header list renders as an empty object");
}

void testTranscodeParams() {
    using navidrome::streamTranscodeParams;
    check(streamTranscodeParams("", 0).empty(),
        "no preferences means no extra stream params");
    check(streamTranscodeParams("mp3", 0) == "&format=mp3",
        "format alone is emitted");
    check(streamTranscodeParams("", 192) == "&maxBitRate=192",
        "bitrate alone is emitted");
    check(streamTranscodeParams("opus", 128) == "&format=opus&maxBitRate=128",
        "both preferences are emitted in order");
    check(streamTranscodeParams("raw", 0) == "&format=raw",
        "raw is passed through as a format");

    using navidrome::effectiveStreamSuffix;
    check(effectiveStreamSuffix("", "flac") == "flac",
        "server default keeps the track's own codec");
    check(effectiveStreamSuffix("raw", "flac") == "flac",
        "raw keeps the track's own codec");
    check(effectiveStreamSuffix("mp3", "flac") == "mp3",
        "transcoding wins over the track's codec for the decoder hint");
    check(effectiveStreamSuffix("mp3", "").empty() == false,
        "transcoding supplies a codec even when the track has none");
}

void testFileNames() {
    using navidrome::sanitizeFileName;
    check(sanitizeFileName("AC/DC: Back in Black?") == "AC_DC_ Back in Black_",
        "path and reserved characters are replaced");
    check(sanitizeFileName("trailing dots...") == "trailing dots",
        "trailing dots are trimmed (Windows rejects them)");
    check(sanitizeFileName("   ") == "untitled",
        "an all-trimmed name falls back to a placeholder");
    check(sanitizeFileName(u8"中文 title") == u8"中文 title",
        "non-ASCII names survive untouched");
    check(sanitizeFileName("a\\b*c<d>e|f\"g") == "a_b_c_d_e_f_g",
        "every remaining Windows-reserved character is replaced");
    check(sanitizeFileName("...") == "untitled",
        "a name that is only dots trims to nothing and falls back");
    check(sanitizeFileName("tab\tinside") == "tab inside",
        "a control character becomes a space");
    check(sanitizeFileName(".hidden") == ".hidden",
        "a leading dot is kept (only trailing dots/spaces are unsafe on Windows)");
}

void testQueryParams() {
    using navidrome::queryParamFromURI;
    const std::string uri =
        "navidrome://track/abc?title=Song&album=Live%20Set&rating=4&starred=1"
        "&albumId=alb%2F42";

    check(queryParamFromURI(uri, "rating") == "4", "a middle parameter is read");
    check(queryParamFromURI(uri, "albumId") == "alb/42",
        "the last parameter is read and percent-decoded");
    // "album=" is a prefix of "albumId=" and vice versa — a naive find() would
    // return the wrong one of the two.
    check(queryParamFromURI(uri, "album") == "Live Set",
        "a parameter whose name prefixes another is not confused with it");
    check(queryParamFromURI(uri, "coverArt").empty(),
        "an absent parameter reads as empty, never as a value");
    check(queryParamFromURI("navidrome://track/abc", "albumId").empty(),
        "a URI with no query at all reads as empty (pre-albumId playlists)");
    check(queryParamFromURI("https://server/music.mp3?albumId=x", "albumId").empty(),
        "a foreign URI is never parsed");
    check(queryParamFromURI("navidrome://track/abc?albumId=", "albumId").empty(),
        "an empty value is indistinguishable from absent, and must stay so");

    // trackIdFromURI shares the percent-decoder; guard the seam.
    check(navidrome::trackIdFromURI("navidrome://track/song%252Fraw?rating=3") ==
        "song%2Fraw", "the song id is decoded exactly once, query stripped");
    check(navidrome::trackIdFromURI("https://server/music.mp3").empty(),
        "a foreign URI yields no song id");

    check(queryParamFromURI("navidrome://track/x?title=a=b&rating=4", "title") == "a=b",
        "a value containing '=' is returned whole (params split on '&', not '=')");
    check(queryParamFromURI("navidrome://track/x?rating=4&rating=5", "rating") == "4",
        "a repeated parameter yields the first occurrence");
    check(queryParamFromURI("navidrome://track/x?title=a+b", "title") == "a+b",
        "'+' is left literal, not turned into a space (this is not form-encoding)");
    check(navidrome::trackIdFromURI("navidrome://track/").empty(),
        "a URI equal to the bare prefix has no song id");
    check(navidrome::trackIdFromURI("NAVIDROME://track/abc").empty(),
        "the scheme match is case-sensitive");
    check(queryParamFromURI("navidrome://track/?rating=4", "rating") == "4" &&
          navidrome::trackIdFromURI("navidrome://track/?rating=4").empty(),
        "an empty id before the query still parses; params still read");
}

void testMd5KnownAnswers() {
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

void testCrossParserParity() {
    using navidrome::resolveArtId;
    using navidrome::trackIdFromURI;
    // resolveArtId (art extractor) and trackIdFromURI (scrobbler) are separate
    // implementations that both pull <id> out of navidrome://track/<id>.
    // CLAUDE.md flags scheme drift between the two as a live trap — pin them to
    // the same decoded output for ids that exercise the decoder.
    const char* ids[] = {"plain", "a/b", "a%2Fb", u8"中文+plus", "x?y"};
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

void testErrorModel() {
    using navidrome::ErrorKind;
    using navidrome::Error;
    using navidrome::httpStatusToErrorKind;
    using navidrome::subsonicCodeToErrorKind;
    using navidrome::isRetryable;
    using navidrome::errorKindName;

    // HTTP status -> ErrorKind
    check(httpStatusToErrorKind(200) == ErrorKind::None, "HTTP 200 is not an error");
    check(httpStatusToErrorKind(204) == ErrorKind::None, "any 2xx is success");
    check(httpStatusToErrorKind(0) == ErrorKind::Network,
        "status 0 (no response line) is a network failure");
    check(httpStatusToErrorKind(401) == ErrorKind::Auth, "HTTP 401 is auth");
    check(httpStatusToErrorKind(403) == ErrorKind::Auth, "HTTP 403 is auth");
    check(httpStatusToErrorKind(404) == ErrorKind::NotFound, "HTTP 404 is not-found");
    check(httpStatusToErrorKind(410) == ErrorKind::NotFound, "HTTP 410 Gone is not-found");
    check(httpStatusToErrorKind(429) == ErrorKind::RateLimited, "HTTP 429 is rate-limited");
    check(httpStatusToErrorKind(500) == ErrorKind::ServerError, "HTTP 500 lower bound");
    check(httpStatusToErrorKind(599) == ErrorKind::ServerError, "HTTP 599 upper bound");
    check(httpStatusToErrorKind(302) == ErrorKind::Network,
        "an unfollowed redirect is a transport problem, not a server error");
    check(httpStatusToErrorKind(418) == ErrorKind::ServerError,
        "an unmapped 4xx falls back to server error");

    // Subsonic error code -> ErrorKind
    check(subsonicCodeToErrorKind(40) == ErrorKind::Auth, "Subsonic 40 wrong creds is auth");
    check(subsonicCodeToErrorKind(41) == ErrorKind::Auth, "Subsonic 41 token auth n/a is auth");
    check(subsonicCodeToErrorKind(50) == ErrorKind::Auth, "Subsonic 50 not authorized is auth");
    check(subsonicCodeToErrorKind(70) == ErrorKind::NotFound, "Subsonic 70 is not-found");
    check(subsonicCodeToErrorKind(0) == ErrorKind::ServerError, "Subsonic 0 generic is server error");
    check(subsonicCodeToErrorKind(10) == ErrorKind::ServerError,
        "Subsonic 10 missing param is a server error to us (we built the request)");
    check(subsonicCodeToErrorKind(60) == ErrorKind::ServerError,
        "Subsonic 60 trial expired is unmapped -> server error");

    // retry policy
    check(isRetryable(ErrorKind::Network), "network failures are retryable");
    check(isRetryable(ErrorKind::Timeout), "timeouts are retryable");
    check(isRetryable(ErrorKind::RateLimited), "rate-limit is retryable (after backoff)");
    check(isRetryable(ErrorKind::ServerError), "5xx is retryable");
    check(!isRetryable(ErrorKind::Auth), "auth failure is deterministic, not retryable");
    check(!isRetryable(ErrorKind::NotFound), "not-found is deterministic, not retryable");
    check(!isRetryable(ErrorKind::Parse), "a parse failure repeats, not retryable");
    check(!isRetryable(ErrorKind::NotConfigured), "not-configured is not retryable");
    check(!isRetryable(ErrorKind::None), "success is not 'retryable'");

    // Error convenience accessors
    Error ok;
    check(ok.ok() && !ok.retryable() && std::string(ok.kindName()) == "None",
        "a default-constructed Error is success");
    Error timedOut{ErrorKind::Timeout, 0, 0, "receive deadline hit"};
    check(!timedOut.ok() && timedOut.retryable(),
        "a Timeout Error reports not-ok and retryable");
    check(std::string(timedOut.kindName()) == "Timeout",
        "kindName round-trips the enum");

    // every enumerator has a distinct, non-empty name
    const ErrorKind all[] = {
        ErrorKind::None, ErrorKind::NotConfigured, ErrorKind::Network,
        ErrorKind::Timeout, ErrorKind::Tls, ErrorKind::Auth, ErrorKind::NotFound,
        ErrorKind::RateLimited, ErrorKind::ServerError, ErrorKind::Parse,
        ErrorKind::Cancelled, ErrorKind::Unknown,
    };
    for (ErrorKind k : all) {
        check(errorKindName(k) != nullptr && errorKindName(k)[0] != '\0',
            "every ErrorKind has a printable name");
    }
}

} // namespace

int main() {
    testUriEncodeDecode();
    testNormalizeUrl();
    testJsEscapeEdgeCases();
    testStarKindAndAlbumListType();
    testParseHeaderLines();
    testPercentDecodeEdgeCases();
    testPlaylistChunkSize();
    testIdentifiers();
    testCoverUrl();
    testClassification();
    testCache();
    testConfig();
    testTranscodeParams();
    testFileNames();
    testQueryParams();
    testMd5KnownAnswers();
    testCrossParserParity();
    testErrorModel();
    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All MediaEnrichment tests passed\n";
    return 0;
}
