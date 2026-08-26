#include "../MediaEnrichmentLogic.h"
// SubsonicTypes.h is pure C++ (no SDK, no Windows headers), so its helpers can
// be exercised from this standalone host executable too.
#include "../../SubsonicTypes.h"

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
}

void testClassification() {
    using navidrome::FetchClass;
    using navidrome::classifyBody;
    using navidrome::classifyHttpStatus;

    check(classifyHttpStatus(200) == FetchClass::Ok, "HTTP 200");
    check(classifyHttpStatus(401) == FetchClass::Auth, "HTTP 401");
    check(classifyHttpStatus(403) == FetchClass::Auth, "HTTP 403");
    check(classifyHttpStatus(404) == FetchClass::NotFound, "HTTP 404");
    check(classifyHttpStatus(503) == FetchClass::ServerError, "HTTP 5xx");

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
    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All MediaEnrichment tests passed\n";
    return 0;
}
