// MediaEnrichmentLogic.{h,cpp} still live under Windows/ (only the Windows
// component links them), but the module is SDK-free and its one platform-
// specific line is MD5 (#if defined(_WIN32) WinCrypt / #else CommonCrypto), so
// this host builds on Windows, Linux (make test) and macOS (make mac-test).
#include "../Windows/MediaEnrichmentLogic.h"
// SubsonicTypes.h is pure C++ (no SDK, no Windows headers), so its helpers can
// be exercised from this standalone host executable too.
#include "../SubsonicTypes.h"
// NavidromeBrowserModel.h is the shared browser tree model (SDK-free) — the
// node struct, category list and row-label formatting used by both platform
// browser views. NavidromeBrowserModel.cpp (built into this host) adds the
// child-fetch dispatch over the IBrowserClient seam.
#include "../NavidromeBrowserModel.h"
#include "../NavidromePlaylistSync.h"

// NavidromeBrowserModel.cpp calls navidrome::syncRatingsToPlaylists after a
// successful child fetch and from syncBrowserNodesToPlaylists; the real
// implementation lives in main.cpp (SDK-only) which this standalone host does
// not link. This stub records what it was handed so the tests can assert the
// node -> RatingUpdate filtering.
namespace navidrome {
std::vector<RatingUpdate> g_lastRatingSync;
void syncRatingsToPlaylists(std::vector<RatingUpdate> u) { g_lastRatingSync = std::move(u); }
}

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

void testTrackURICodec() {
    using navidrome::TrackURI;
    using navidrome::buildTrackURI;
    using navidrome::parseTrackURI;

    // Empty id -> empty URI (both platforms rely on this to skip non-songs).
    check(buildTrackURI(TrackURI{}).empty(), "an empty id builds no URI");

    // A bare track carrying only an id must be byte-identical to what the
    // pre-codec builders produced: prefix + encoded id, no '?'.
    TrackURI bare;
    bare.id = "abc123";
    check(buildTrackURI(bare) == "navidrome://track/abc123",
        "an id-only track has no query string");

    // Full round-trip. Note album has a space and albumId a slash — both must
    // survive encode -> decode unchanged.
    TrackURI in;
    in.id       = "song/42";
    in.title    = "Café del Mar";
    in.artist   = "A & B";
    in.album    = "Live Set";
    in.albumId  = "alb/7";
    in.coverArtId = "art-9";
    in.suffix   = "flac";
    in.track    = 3;
    in.year     = 2011;
    in.rating   = 4;
    in.duration = 251.4;
    in.starred  = true;

    const std::string uri = buildTrackURI(in);
    check(uri.compare(0, 18, "navidrome://track/") == 0, "URI keeps the scheme prefix");
    // Reserved characters in the id and values are percent-encoded.
    check(uri.find("navidrome://track/song%2F42") == 0, "the id is percent-encoded");
    check(uri.find("artist=A%20%26%20B") != std::string::npos,
        "'&' and space in a value are escaped so they don't break the query");
    check(uri.find("albumId=alb%2F7") != std::string::npos, "albumId is escaped");

    const TrackURI out = parseTrackURI(uri);
    check(out.id == in.id,             "id round-trips");
    check(out.title == in.title,       "unicode title round-trips");
    check(out.artist == in.artist,     "artist with '&' round-trips");
    check(out.album == in.album,       "album with space round-trips");
    check(out.albumId == in.albumId,   "albumId round-trips");
    check(out.coverArtId == in.coverArtId, "coverArt id round-trips");
    check(out.suffix == in.suffix,     "suffix round-trips");
    check(out.track == in.track,       "track number round-trips");
    check(out.year == in.year,         "year round-trips");
    check(out.rating == in.rating,     "rating round-trips");
    check(out.starred == in.starred,   "starred round-trips");
    check(out.duration > 251.0 && out.duration < 252.0, "duration round-trips (approx)");

    // Unset fields are omitted, not sent as empty params.
    TrackURI minimal;
    minimal.id = "x";
    minimal.title = "T";
    const std::string mUri = buildTrackURI(minimal);
    check(mUri == "navidrome://track/x?title=T", "only set fields appear in the query");
    check(mUri.find("rating=") == std::string::npos, "an unset rating is absent, not rating=0");
    check(mUri.find("starred=") == std::string::npos, "an unset star is absent");

    // A foreign URI parses to an empty id (== "not ours").
    check(parseTrackURI("https://server/music.mp3?title=x").id.empty(),
        "a non-navidrome URI yields no id");
    check(parseTrackURI("navidrome://track/").id.empty(),
        "the bare prefix yields no id");

    // An old URI missing the newer fields leaves them at defaults, never a sentinel.
    const TrackURI legacy = parseTrackURI("navidrome://track/old?title=Song&artist=Nine");
    check(legacy.id == "old" && legacy.title == "Song" && legacy.artist == "Nine",
        "a legacy URI's known fields parse");
    check(legacy.rating == 0 && !legacy.starred && legacy.albumId.empty(),
        "a legacy URI's absent fields stay at their defaults");

    // Unknown query keys are ignored, not fatal.
    const TrackURI fwd = parseTrackURI("navidrome://track/y?title=Z&future=1&rating=2");
    check(fwd.title == "Z" && fwd.rating == 2, "an unknown key is skipped, known keys still read");
}

void testScrobbleThreshold() {
    using navidrome::scrobbleSubmitThreshold;
    // Half the length while that's under the 4-minute cap.
    check(scrobbleSubmitThreshold(200.0) == 100.0, "short track: submit at half length");
    // Capped at 240s for anything 8 minutes or longer.
    check(scrobbleSubmitThreshold(600.0) == 240.0, "long track: submit is capped at 4 min");
    check(scrobbleSubmitThreshold(480.0) == 240.0, "exactly 8 min: half == cap");
    // Unknown / live-stream length falls back to the cap alone.
    check(scrobbleSubmitThreshold(0.0) == 240.0, "unknown length falls back to the cap");
    check(scrobbleSubmitThreshold(-1.0) == 240.0, "negative length falls back to the cap");
}

void testMusicFolderFilter() {
    using navidrome::MusicFolder;
    using navidrome::parseMusicFolderIds;
    using navidrome::joinMusicFolderIds;
    using navidrome::effectiveMusicFolderIds;

    // parse: trim, drop empties, de-dupe, keep order.
    const auto ids = parseMusicFolderIds(" 1, 2 ,,3, 2 ");
    check((ids == std::vector<std::string>{"1", "2", "3"}),
        "parseMusicFolderIds trims, de-dupes and drops empty entries");
    check(parseMusicFolderIds("").empty(), "empty csv parses to no ids");
    check(parseMusicFolderIds("  ,  , ").empty(),
        "a csv of only separators/space parses to no ids");
    check(joinMusicFolderIds({"1", "2", "3"}) == "1,2,3",
        "joinMusicFolderIds is the inverse form");
    check(joinMusicFolderIds({}).empty(), "joining nothing yields an empty string");

    const std::vector<MusicFolder> one   = {{"1", "Music"}};
    const std::vector<MusicFolder> two    = {{"1", "Music"}, {"2", "Audiobooks"}};
    const std::vector<MusicFolder> three  = {{"1", "Music"}, {"2", "Audiobooks"}, {"3", "Podcasts"}};

    // Every "do nothing" branch returns {} — byte-for-byte today's behaviour.
    check(effectiveMusicFolderIds(false, "1", two).empty(),
        "filter disabled -> no fan-out even with a selection");
    check(effectiveMusicFolderIds(true, "", two).empty(),
        "empty selection -> no fan-out");
    check(effectiveMusicFolderIds(true, "1", one).empty(),
        "server with a single library -> no fan-out");
    check(effectiveMusicFolderIds(true, "1,2", two).empty(),
        "selection covering every server folder -> one unfiltered request");
    check(effectiveMusicFolderIds(true, "7,8,9", two).empty(),
        "a selection that is entirely stale -> no fan-out");

    // Real subset -> fan-out list, in server order, stale ids dropped.
    check((effectiveMusicFolderIds(true, "1", two) == std::vector<std::string>{"1"}),
        "one-of-two selected -> fan out over that id");
    check((effectiveMusicFolderIds(true, "3,1", three) ==
           std::vector<std::string>{"1", "3"}),
        "fan-out list follows server order, not selection order");
    check((effectiveMusicFolderIds(true, "2,9", three) ==
           std::vector<std::string>{"2"}),
        "a stale id in an otherwise valid selection is dropped");
}

void testRawQueryParam() {
    using navidrome::rawQueryParam;
    const std::string legacy =
        "https://s/rest/stream.view?id=song%2F1&coverArt=art%2F9&u=me";
    check(rawQueryParam(legacy, "coverArt") == "art/9",
        "a param is read out of a plain HTTP url and percent-decoded");
    check(rawQueryParam(legacy, "id") == "song/1", "the first param is read");
    check(rawQueryParam(legacy, "size").empty(), "an absent param reads empty");
    check(rawQueryParam("navidrome://track/x", "id").empty(),
        "no query string means empty, not a crash");
    // Pair-boundary anchoring: "id" must not match inside "guid=".
    check(rawQueryParam("x?guid=abc&id=real", "id") == "real",
        "a param name is only matched at a pair boundary");
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

void testFanOutMerge() {
    using navidrome::Album;
    auto id = [](const Album& a) { return a.id; };
    auto mk = [](const char* i) { Album a; a.id = i; return a; };

    // empty folder list -> one call with an empty id, result passed straight through
    {
        int calls = 0;
        auto out = navidrome::mergeFanOut<Album>({}, [&](const std::string& fid) {
            ++calls;
            check(fid.empty(), "empty folder list calls fetch once with no id");
            return std::vector<Album>{ mk("a"), mk("b") };
        }, id);
        check(calls == 1 && out.size() == 2, "single-request path");
    }

    // two folders, overlapping ids -> merged, first occurrence wins, order kept
    {
        auto out = navidrome::mergeFanOut<Album>({"1", "2"}, [&](const std::string& fid) {
            if (fid == "1") return std::vector<Album>{ mk("a"), mk("b") };
            return std::vector<Album>{ mk("b"), mk("c") };
        }, id);
        check(out.size() == 3, "duplicate id dropped across folders");
        check(out[0].id == "a" && out[1].id == "b" && out[2].id == "c",
            "merge preserves first-seen order");
    }

    // items with an empty id are never treated as duplicates
    {
        auto out = navidrome::mergeFanOut<Album>({"1", "2"}, [&](const std::string&) {
            return std::vector<Album>{ mk("") };
        }, id);
        check(out.size() == 2, "empty-id items are all kept");
    }
}

void testAlbumArtistFilter() {
    using navidrome::Album;
    auto mk = [](const char* i, const char* artist) {
        Album a; a.id = i; a.artistId = artist; return a;
    };

    std::vector<Album> fromArtist = { mk("al1", "art1"), mk("al2", "art1"), mk("al3", "art1") };

    // search confirms al1 + al3 belong to art1 (al2 only in another library)
    {
        std::vector<Album> search = { mk("al1", "art1"), mk("al3", "art1"), mk("alX", "other") };
        bool unconfirmed = true;
        auto out = navidrome::filterAlbumsByArtistSearch(fromArtist, "art1", search, unconfirmed);
        check(!unconfirmed, "a non-empty allow-set is 'confirmed'");
        check(out.size() == 2 && out[0].id == "al1" && out[1].id == "al3",
            "keeps only confirmed albums, order preserved");
    }

    // search returned nothing for this artist -> full list back, flagged
    {
        std::vector<Album> search = { mk("alX", "other") };
        bool unconfirmed = false;
        auto out = navidrome::filterAlbumsByArtistSearch(fromArtist, "art1", search, unconfirmed);
        check(unconfirmed, "empty allow-set sets outUnconfirmed");
        check(out.size() == 3, "and returns the unfiltered list");
    }

    // an album search row with no id is ignored
    {
        std::vector<Album> search = { mk("", "art1") };
        bool unconfirmed = false;
        auto out = navidrome::filterAlbumsByArtistSearch(fromArtist, "art1", search, unconfirmed);
        check(unconfirmed && out.size() == 3, "a blank-id search row doesn't confirm anything");
    }
}

void testPrefsOptions() {
    const auto& fmts = navidrome::streamFormatOptions();
    check(fmts.size() == 7, "7 transcode format rows");
    check(std::string(fmts.front().value).empty() && std::string(fmts.front().label) == "Server default",
        "first row is the server-default (empty value)");
    check(std::string(fmts[1].value) == "raw", "second row forces the original file");
    check(std::string(fmts[2].value) == "mp3" && std::string(fmts.back().value) == "wav",
        "mp3 first real codec, wav last");

    const auto& br = navidrome::maxBitrateOptions();
    check(br.size() == 7 && br.front() == 0 && br.back() == 320,
        "bitrate ceilings 0..320, 0 = unlimited");

    check(navidrome::kScanPollIntervalMs == 1500, "rescan re-poll interval");
}

void testRetryPolicy() {
    using navidrome::Error;
    using navidrome::ErrorKind;
    namespace retry = navidrome::retry;

    check(retry::kMaxAttempts == 3, "3 attempts total");

    Error transient{ErrorKind::Timeout, 0, 0, "t"};
    Error fatal{ErrorKind::Auth, 401, 0, "a"};
    Error ok;

    check(retry::again(transient, 1), "retry a transient failure after attempt 1");
    check(retry::again(transient, 2), "retry a transient failure after attempt 2");
    check(!retry::again(transient, 3), "no retry after the last attempt");
    check(!retry::again(fatal, 1), "never retry a deterministic failure");
    check(!retry::again(ok, 1), "nothing to retry on success");

    check(retry::backoffMs(1, 0) == 300, "attempt 1 backoff is 300ms + jitter");
    check(retry::backoffMs(2, 0) == 600, "attempt 2 backoff is 600ms + jitter");
    check(retry::backoffMs(1, 199) == 499, "jitter is added on top");
}

void testScrobbleTracker() {
    // A real navidrome:// URI (10s track) and something that isn't ours.
    const std::string ours = "navidrome://track/s1?duration=10";
    const std::string alien = "https://example.com/song.mp3";
    const double len = 10.0;

    // --- not one of ours: nothing happens ---
    {
        navidrome::ScrobbleTracker t;
        auto a = t.onNewTrack(alien, len, true);
        check(a.refreshRatingId.empty() && a.scrobbleNowId.empty(),
            "an alien track triggers no refresh and no scrobble");
        check(t.onPlaybackTime(9.0).empty(), "and never submits");
    }

    // --- ours, scrobbling OFF: refresh only, never a scrobble ---
    {
        navidrome::ScrobbleTracker t;
        auto a = t.onNewTrack(ours, len, false);
        check(a.refreshRatingId == "s1", "rating refresh fires even with scrobbling off");
        check(a.scrobbleNowId.empty(), "no now-playing scrobble when the pref is off");
        check(t.onPlaybackTime(999.0).empty(), "no submission when scrobbling is off");
    }

    // --- ours, scrobbling ON: refresh + now-playing, then submit once ---
    {
        navidrome::ScrobbleTracker t;
        auto a = t.onNewTrack(ours, len, true);
        check(a.refreshRatingId == "s1" && a.scrobbleNowId == "s1",
            "refresh + now-playing both fire for our track with scrobbling on");
        const double thr = navidrome::scrobbleSubmitThreshold(len);
        check(t.onPlaybackTime(thr - 0.01).empty(), "no submit before the threshold");
        check(t.onPlaybackTime(thr + 0.01) == "s1", "submits once the threshold is crossed");
        check(t.onPlaybackTime(thr + 5.0).empty(), "does not submit again");
    }

    // --- onStop resets, so the same instance is reusable across tracks ---
    {
        navidrome::ScrobbleTracker t;
        t.onNewTrack(ours, len, true);
        t.onPlaybackTime(999.0);   // submitted
        t.onStop();
        check(t.onPlaybackTime(999.0).empty(), "after onStop there is nothing to submit");
        auto a = t.onNewTrack(ours, len, true);
        check(a.scrobbleNowId == "s1", "a fresh track after stop is tracked again");
        check(t.onPlaybackTime(999.0) == "s1", "and can submit again");
    }
}

void testSessionEnv() {
    navidrome::SessionEnv e;
    e.platform        = "Windows";
    e.configured      = true;
    e.serverUrl       = "https://music.example";
    e.transcodeFormat = "";
    e.maxBitrate      = 192;
    e.scrobble        = true;
    e.startupRefresh  = false;
    e.customHeaders   = true;
    const std::string s = navidrome::describeSessionEnv(e);
    check(s == "platform=Windows  configured=yes  server=https://music.example  "
               "transcode=server-default  maxBitrate=192  scrobble=on  "
               "startupRefresh=off  customHeaders=yes",
        "describeSessionEnv formats the whole line, empty format -> server-default");

    navidrome::SessionEnv d;
    d.platform = "macOS";
    d.transcodeFormat = "opus";
    check(navidrome::describeSessionEnv(d) ==
          "platform=macOS  configured=no  server=  transcode=opus  maxBitrate=0  "
          "scrobble=off  startupRefresh=off  customHeaders=no",
        "defaults render as no/off/0 and an explicit format passes through");
}

void testJson() {
    using navidrome::json::Value;
    std::string err;

    // --- primitives ---
    Value n = navidrome::json::parse("  42  ", err);
    check(err.empty() && n.type() == Value::Number && n.asNumber() == 42.0,
        "parses a bare number with surrounding whitespace");
    check(navidrome::json::parse("-3.5e2", err).asNumber() == -350.0 && err.empty(),
        "parses a signed number with exponent");
    check(navidrome::json::parse("true", err).asBool() == true && err.empty(),
        "parses true");
    check(navidrome::json::parse("false", err).asBool() == false && err.empty(),
        "parses false");
    check(navidrome::json::parse("null", err).isNull() && err.empty(),
        "parses null");

    // --- strings + escapes ---
    Value s = navidrome::json::parse("\"a\\\"b\\\\c\\/d\\n\"", err);
    check(err.empty() && s.type() == Value::String && s.asString() == "a\"b\\c/d\n",
        "decodes \\\" \\\\ \\/ \\n escapes");
    check(navidrome::json::parse("\"caf\\u00e9\"", err).asString() == "caf\xC3\xA9" && err.empty(),
        "\\u00e9 decodes to 2-byte UTF-8");
    check(navidrome::json::parse("\"\\uD83D\\uDE00\"", err).asString() == "\xF0\x9F\x98\x80" && err.empty(),
        "a surrogate pair decodes to 4-byte UTF-8");

    // --- object + array, missing-key safety ---
    Value o = navidrome::json::parse(
        "{\"a\":1,\"b\":[10,20,30],\"c\":{\"d\":\"x\"},\"e\":null}", err);
    check(err.empty() && o.isObject() && o.size() == 4, "object with 4 members");
    check(o["a"].asNumber() == 1.0, "object[\"a\"]");
    check(o["b"].isArray() && o["b"].size() == 3 && o["b"][1u].asNumber() == 20.0,
        "nested array indexing");
    check(o["c"]["d"].asString() == "x", "nested object indexing");
    check(o["missing"].isNull() && o["missing"]["deeper"].isNull(),
        "a missing key chains to Null without faulting");
    check(o["a"].asString().empty() && o["b"].asNumber(-1) == -1.0,
        "type-mismatched access returns the default");
    check(o.has("e") && o["e"].isNull(), "has() is true for an explicit null");
    check(!o.has("nope"), "has() is false for an absent key");

    // --- items(): Subsonic single-element-array collapse ---
    Value arr = navidrome::json::parse("{\"x\":[{\"id\":1},{\"id\":2}]}", err);
    check(arr["x"].items().size() == 2, "items() over a real array");
    Value one = navidrome::json::parse("{\"x\":{\"id\":9}}", err);
    check(one["x"].items().size() == 1 && (*one["x"].items()[0])["id"].asNumber() == 9.0,
        "items() wraps a collapsed single object");
    check(navidrome::json::parse("{}", err)["x"].items().empty(),
        "items() over a missing field is empty");

    // --- error cases ---
    navidrome::json::parse("{\"a\":}", err);
    check(!err.empty(), "reports an error on a missing value");
    navidrome::json::parse("[1,2", err);
    check(!err.empty(), "reports an error on an unterminated array");
    navidrome::json::parse("{\"a\":1} trailing", err);
    check(!err.empty(), "rejects trailing content after the value");
    navidrome::json::parse("\"unterminated", err);
    check(!err.empty(), "reports an error on an unterminated string");
}

void testSubsonicParsers() {
    std::string err;
    auto parse = [&](const std::string& body) {
        return navidrome::json::parse(body, err);
    };

    // --- parseSong: defaults, starred-by-presence, numeric id coercion ---
    navidrome::Song s1 = navidrome::parseSong(parse(
        "{\"id\":\"t1\",\"title\":\"Song\",\"artist\":\"A\",\"artistId\":42,"
        "\"album\":\"Alb\",\"albumId\":\"al1\",\"coverArt\":\"c1\",\"suffix\":\"flac\","
        "\"track\":3,\"year\":2001,\"duration\":123.5,\"starred\":\"2020-01-01T00:00:00Z\","
        "\"userRating\":4}"));
    check(s1.id == "t1" && s1.title == "Song" && s1.artist == "A", "parseSong basic fields");
    check(s1.artistId == "42", "parseSong coerces a numeric artistId to string");
    check(s1.track == 3 && s1.year == 2001 && s1.duration == 123.5, "parseSong numerics");
    check(s1.starred && s1.rating == 4, "parseSong: starred by key presence, rating from userRating");

    navidrome::Song s2 = navidrome::parseSong(parse("{\"id\":\"t2\"}"));
    check(s2.title == "Unknown Title" && !s2.starred && s2.rating == 0 && s2.duration == 0.0,
        "parseSong defaults when fields are absent");

    // --- parseAlbum ---
    navidrome::Album al = navidrome::parseAlbum(parse(
        "{\"id\":\"al1\",\"name\":\"Rec\",\"artist\":\"A\",\"artistId\":\"ar1\","
        "\"coverArt\":\"c\",\"year\":1999,\"songCount\":10,\"starred\":\"x\"}"));
    check(al.id == "al1" && al.name == "Rec" && al.year == 1999 && al.songCount == 10 && al.starred,
        "parseAlbum fields");
    check(navidrome::parseAlbum(parse("{}")).name == "Unknown Album",
        "parseAlbum name default");

    // --- parseArtist ---
    navidrome::Artist ar = navidrome::parseArtist(parse(
        "{\"id\":\"ar1\",\"name\":\"Band\",\"albumCount\":7}"));
    check(ar.id == "ar1" && ar.name == "Band" && ar.albumCount == 7, "parseArtist fields");
    check(navidrome::parseArtist(parse("{}")).name == "Unknown Artist",
        "parseArtist name default");

    // --- parsePlaylist ---
    navidrome::Playlist pl = navidrome::parsePlaylist(parse(
        "{\"id\":9,\"name\":\"Mix\",\"owner\":\"me\",\"songCount\":4,\"duration\":88.0}"));
    check(pl.id == "9" && pl.name == "Mix" && pl.owner == "me" && pl.songCount == 4,
        "parsePlaylist fields, numeric id coerced");

    // --- parseGenre: the "value"-not-"name" quirk ---
    navidrome::Genre g = navidrome::parseGenre(parse(
        "{\"value\":\"Jazz\",\"songCount\":12,\"albumCount\":3}"));
    check(g.name == "Jazz" && g.songCount == 12 && g.albumCount == 3,
        "parseGenre reads the genre string from \"value\"");

    // --- parseMusicFolder: id arrives as a JSON number ---
    navidrome::MusicFolder mf = navidrome::parseMusicFolder(parse(
        "{\"id\":1,\"name\":\"Music\"}"));
    check(mf.id == "1" && mf.name == "Music", "parseMusicFolder coerces a numeric id");

    // --- parseRadioStation ---
    navidrome::RadioStation st = navidrome::parseRadioStation(parse(
        "{\"id\":\"r1\",\"name\":\"Radio\",\"streamUrl\":\"http://s/\",\"homePageUrl\":\"http://h/\"}"));
    check(st.id == "r1" && st.streamUrl == "http://s/" && st.homePageUrl == "http://h/",
        "parseRadioStation fields");
    check(navidrome::parseRadioStation(parse("{\"id\":\"r2\"}")).name == "Unnamed station",
        "parseRadioStation name default");

    // --- parseBookmark: wraps an "entry" song + ms position ---
    navidrome::Bookmark bm;
    bool okBm = navidrome::parseBookmark(parse(
        "{\"position\":45000,\"comment\":\"resume\",\"entry\":{\"id\":\"t9\",\"title\":\"X\"}}"), bm);
    check(okBm && bm.song.id == "t9" && bm.positionMs == 45000.0 && bm.comment == "resume",
        "parseBookmark extracts the entry song and ms position");
    check(!navidrome::parseBookmark(parse("{\"position\":1}"), bm),
        "parseBookmark returns false with no entry");

    // --- parseScanStatus: array-collapsed scanStatus ---
    navidrome::ScanStatus ss = navidrome::parseScanStatus(parse(
        "{\"scanStatus\":{\"scanning\":true,\"count\":1234}}"));
    check(ss.scanning && ss.count == 1234, "parseScanStatus reads scanning + count");

    // --- parseSubsonicResponse: envelope handling ---
    navidrome::SubsonicResponse okResp = navidrome::parseSubsonicResponse(
        "{\"subsonic-response\":{\"status\":\"ok\",\"version\":\"1.16.1\","
        "\"songsByGenre\":{\"song\":[{\"id\":\"a\"},{\"id\":\"b\"}]}}}");
    check(okResp.ok && okResp.error.ok(), "parseSubsonicResponse: status ok");
    check(okResp.inner()["songsByGenre"]["song"].items().size() == 2,
        "parseSubsonicResponse: payload walkable off inner()");

    navidrome::SubsonicResponse errResp = navidrome::parseSubsonicResponse(
        "{\"subsonic-response\":{\"status\":\"failed\","
        "\"error\":{\"code\":40,\"message\":\"Wrong username or password\"}}}");
    check(!errResp.ok && errResp.error.kind == navidrome::ErrorKind::Auth &&
          errResp.error.code == 40 && errResp.error.message == "Wrong username or password",
        "parseSubsonicResponse maps a Subsonic error code to ErrorKind");

    navidrome::SubsonicResponse badJson = navidrome::parseSubsonicResponse("not json{");
    check(!badJson.ok && badJson.error.kind == navidrome::ErrorKind::Parse,
        "parseSubsonicResponse: invalid JSON -> Parse error");

    navidrome::SubsonicResponse noWrap = navidrome::parseSubsonicResponse("{\"foo\":1}");
    check(!noWrap.ok && noWrap.error.kind == navidrome::ErrorKind::Parse,
        "parseSubsonicResponse: missing subsonic-response wrapper -> Parse error");
}

void testBrowserModel() {
    using navidrome::BrowserNode;

    // --- category list: canonical order, titles, all Category type ---
    auto cats = navidrome::buildCategoryNodes();
    check(cats.size() == 9, "buildCategoryNodes returns the 9 smart lists");
    const BrowserNode::CategoryKind expectedOrder[] = {
        BrowserNode::CatStarred, BrowserNode::CatRecentlyAdded,
        BrowserNode::CatMostPlayed, BrowserNode::CatRecentlyPlayed,
        BrowserNode::CatRandom, BrowserNode::CatGenres,
        BrowserNode::CatPlaylists, BrowserNode::CatBookmarks,
        BrowserNode::CatRadio,
    };
    bool orderOk = cats.size() == 9;
    for (size_t i = 0; i < cats.size() && orderOk; ++i)
        orderOk = cats[i]->type == BrowserNode::Category &&
                  cats[i]->category == expectedOrder[i] &&
                  !cats[i]->displayName.empty();
    check(orderOk, "category nodes are in canonical order with non-empty titles");
    check(cats[0]->displayName == "\xE2\x98\x85 Starred", "Starred keeps its icon prefix");
    check(cats[7]->category == BrowserNode::CatBookmarks &&
          cats[7]->displayName == "Bookmarks",
          "Bookmarks sits between Playlists and Radio");

    // --- album-list category mapping ---
    check(navidrome::albumListTypeForCategory(BrowserNode::CatRecentlyAdded) ==
          navidrome::AlbumListType::Newest, "RecentlyAdded -> Newest");
    check(navidrome::albumListTypeForCategory(BrowserNode::CatMostPlayed) ==
          navidrome::AlbumListType::Frequent, "MostPlayed -> Frequent");
    check(navidrome::albumListTypeForCategory(BrowserNode::CatRecentlyPlayed) ==
          navidrome::AlbumListType::Recent, "RecentlyPlayed -> Recent");
    check(navidrome::albumListTypeForCategory(BrowserNode::CatRandom) ==
          navidrome::AlbumListType::Random, "Random -> Random");

    // --- model -> node mappers ---
    navidrome::Song s;
    s.id = "s1"; s.title = "Song"; s.artist = "A"; s.album = "Alb";
    s.albumId = "alb1"; s.suffix = "flac"; s.track = 4; s.year = 2001;
    s.duration = 183.0; s.starred = true; s.rating = 3;
    auto sn = navidrome::makeSongNode(s, 42000.0);
    check(sn->type == BrowserNode::Song && sn->id == "s1" && sn->album == "Alb" &&
          sn->albumId == "alb1" && sn->suffix == "flac" && sn->track == 4 &&
          sn->rating == 3 && sn->starred && sn->bookmarkPositionMs == 42000.0 &&
          sn->childrenLoaded,
          "makeSongNode copies every field and marks the node a loaded leaf");

    navidrome::Album a; a.id = "al1"; a.name = "Album"; a.artist = "Artist";
    a.coverArtId = "c1"; a.starred = true;
    auto an = navidrome::makeAlbumNode(a);
    check(an->type == BrowserNode::Album && an->id == "al1" &&
          an->subtitle == "Artist" && an->coverArtId == "c1" && an->starred &&
          !an->childrenLoaded,
          "makeAlbumNode maps id/name/artist/cover/starred and stays expandable");

    navidrome::Artist ar; ar.id = "ar1"; ar.name = "The Artist"; ar.starred = false;
    auto arn = navidrome::makeArtistNode(ar);
    check(arn->type == BrowserNode::Artist && arn->id == "ar1" &&
          arn->displayName == "The Artist", "makeArtistNode maps id/name");

    navidrome::Playlist p; p.id = "p1"; p.name = "Mix"; p.songCount = 1;
    check(navidrome::makePlaylistNode(p)->subtitle == "1 track",
          "playlist subtitle is singular for one track");
    p.songCount = 12;
    check(navidrome::makePlaylistNode(p)->subtitle == "12 tracks",
          "playlist subtitle is plural otherwise");

    navidrome::Genre g; g.name = "Jazz"; g.songCount = 7;
    auto gn = navidrome::makeGenreNode(g);
    check(gn->id == "Jazz" && gn->displayName == "Jazz" && gn->subtitle == "7 tracks",
          "genre node keys id off the name (no genre id in Subsonic)");

    navidrome::RadioStation rs; rs.id = "r1"; rs.name = "SomaFM";
    rs.homePageUrl = "https://somafm.com";
    auto rn = navidrome::makeRadioNode(rs);
    check(rn->type == BrowserNode::Radio && rn->subtitle == "https://somafm.com" &&
          rn->childrenLoaded, "radio node is a loaded leaf carrying the home URL");

    auto ln = navidrome::makeLibraryNode("2", "Podcasts");
    check(ln->type == BrowserNode::Library && ln->id == "2" &&
          ln->displayName == "Podcasts", "library node carries folder id + name");

    check(navidrome::isLeaf(*sn) && navidrome::isLeaf(*rn) &&
          !navidrome::isLeaf(*an) && !navidrome::isLeaf(*arn),
          "isLeaf: songs/radio are leaves, artists/albums expand");

    // --- row display ---
    navidrome::NodeDisplay d = navidrome::nodeDisplay(*sn);
    check(d.name == "\xE2\x98\x85 4. Song",
          "song row: track-number prefix then favorite marker");
    check(d.ratingStars == "\xE2\x98\x85\xE2\x98\x85\xE2\x98\x85",
          "rating renders as N stars, separate from the name");
    check(d.durationText == "3:03", "duration formats as M:SS");
    check(d.bookmarkText.rfind("\xE2\x8F\xB1", 0) == 0 &&
          d.bookmarkText.find("0:42") != std::string::npos,
          "bookmark position renders as a clock glyph + M:SS");

    // Single-column label (Win32) concatenates the pieces; a category row keeps
    // its icon and is never given a star prefix.
    check(navidrome::singleColumnLabel(*sn) ==
          "\xE2\x98\x85 4. Song  \xE2\x98\x85\xE2\x98\x85\xE2\x98\x85  " + d.bookmarkText,
          "singleColumnLabel joins name + rating + bookmark with two spaces");
    check(navidrome::nodeDisplay(*cats[0]).name == "\xE2\x98\x85 Starred",
          "a starred-looking category title is not double-prefixed");

    navidrome::Song plain; plain.id = "s2"; plain.title = "Plain";
    auto pn = navidrome::makeSongNode(plain);
    navidrome::NodeDisplay pd = navidrome::nodeDisplay(*pn);
    check(pd.name == "Plain" && pd.ratingStars.empty() && pd.bookmarkText.empty() &&
          pd.durationText.empty(),
          "an unrated, unbookmarked, zero-length song shows just its title");
    check(navidrome::singleColumnLabel(*pn) == "Plain",
          "singleColumnLabel adds nothing when there are no markers");
}

// A recording IBrowserClient: every call appends its name to `calls` and
// returns one canned item so the dispatch can be asserted without a network.
struct FakeBrowserClient : navidrome::IBrowserClient {
    std::vector<std::string> calls;
    std::string error;                 // set non-empty to simulate a failure
    std::vector<std::string> groupIds; // set 2+ to exercise the library grouping

    template <class T> std::vector<T> one(const char* name, std::string& e, T v) {
        calls.push_back(name);
        e = error;
        if (!error.empty()) return {};
        return { std::move(v) };
    }

    std::vector<navidrome::Artist> getArtists(std::string& e) override {
        navidrome::Artist a; a.id = "ar1"; a.name = "Artist";
        return one("getArtists", e, a);
    }
    std::vector<navidrome::Artist> getArtistsForLibrary(const std::string& lib,
                                                        std::string& e) override {
        navidrome::Artist a; a.id = "ar-" + lib; a.name = "LibArtist";
        return one("getArtistsForLibrary", e, a);
    }
    std::vector<navidrome::Album> getAlbumsForArtist(const std::string&,
                                                     const std::string& scope,
                                                     std::string& e) override {
        calls.push_back("getAlbumsForArtist:" + scope);
        e = error;
        if (!error.empty()) return {};
        navidrome::Album a; a.id = "al1"; a.name = "Album"; a.artist = "Artist";
        return { a };
    }
    std::vector<navidrome::Song> getSongsForAlbum(const std::string&, std::string& e) override {
        navidrome::Song s; s.id = "s1"; s.title = "Track"; return one("getSongsForAlbum", e, s);
    }
    std::vector<navidrome::Song> getPlaylistSongs(const std::string&, std::string& e) override {
        navidrome::Song s; s.id = "s2"; s.title = "PL"; return one("getPlaylistSongs", e, s);
    }
    std::vector<navidrome::Song> getSongsForGenre(const std::string&, int count,
                                                  std::string& e) override {
        calls.push_back("getSongsForGenre:" + std::to_string(count));
        e = error;
        if (!error.empty()) return {};
        navidrome::Song s; s.id = "s3"; s.title = "G"; return { s };
    }
    std::vector<navidrome::Song> getStarredSongs(std::string& e) override {
        navidrome::Song s; s.id = "s4"; s.title = "Fav"; return one("getStarredSongs", e, s);
    }
    std::vector<navidrome::Genre> getGenres(std::string& e) override {
        navidrome::Genre g; g.name = "Rock"; g.songCount = 3; return one("getGenres", e, g);
    }
    std::vector<navidrome::Playlist> getPlaylists(std::string& e) override {
        navidrome::Playlist p; p.id = "p1"; p.name = "Mix"; p.songCount = 2;
        return one("getPlaylists", e, p);
    }
    std::vector<navidrome::Album> getAlbumList(navidrome::AlbumListType t, int size,
                                               std::string& e) override {
        calls.push_back(std::string("getAlbumList:") +
                        navidrome::albumListTypeName(t) + ":" + std::to_string(size));
        e = error;
        if (!error.empty()) return {};
        navidrome::Album a; a.id = "al2"; a.name = "Newest"; return { a };
    }
    std::vector<navidrome::RadioStation> getRadioStations(std::string& e) override {
        navidrome::RadioStation r; r.id = "r1"; r.name = "Radio";
        return one("getRadioStations", e, r);
    }
    std::vector<navidrome::Bookmark> getBookmarks(std::string& e) override {
        navidrome::Bookmark b; b.song.id = "s5"; b.song.title = "Resume"; b.positionMs = 5000;
        return one("getBookmarks", e, b);
    }
    std::vector<std::string> groupingLibraryIds() override {
        calls.push_back("groupingLibraryIds");
        return groupIds;
    }
    std::vector<navidrome::MusicFolder> musicFolders() override {
        calls.push_back("musicFolders");
        return { {"1", "Music"}, {"2", "Podcasts"} };
    }
};

void testBrowserFetchDispatch() {
    using navidrome::BrowserNode;

    // --- buildRootNodes: flat vs. library-grouped ---
    {
        FakeBrowserClient fc;
        std::string err;
        auto roots = navidrome::buildRootNodes(fc, err);
        check(err.empty(), "flat root load reports no error");
        check(roots.size() == 10 && roots.back()->type == BrowserNode::Artist,
              "flat roots = 9 categories + the artist list");
        check(roots.front()->type == BrowserNode::Category,
              "categories come first in the root list");
    }
    {
        FakeBrowserClient fc; fc.groupIds = {"1", "2"};
        std::string err;
        auto roots = navidrome::buildRootNodes(fc, err);
        check(roots.size() == 11, "grouped roots = 9 categories + 2 library nodes");
        check(roots[9]->type == BrowserNode::Library && roots[9]->id == "1" &&
              roots[9]->displayName == "Music" && roots[10]->displayName == "Podcasts",
              "library nodes carry the folder id and resolved name");
        bool calledGetArtists = false;
        for (auto& c : fc.calls) if (c == "getArtists") calledGetArtists = true;
        check(!calledGetArtists, "grouped load never calls the flat getArtists");
    }
    {
        FakeBrowserClient fc; fc.error = "boom";
        std::string err;
        auto roots = navidrome::buildRootNodes(fc, err);
        check(err == "boom", "a flat-list failure propagates the error");
        check(roots.empty(), "no category nodes are returned on a failed root load");
    }

    // --- fetchChildren: one representative case per node type ---
    struct Case {
        BrowserNode node;
        const char* wantCall;
        BrowserNode::Type wantChildType;
    };
    auto artist = [](const char* lib) {
        BrowserNode n; n.type = BrowserNode::Artist; n.id = "ar1"; n.libraryId = lib; return n;
    };
    auto cat = [](BrowserNode::CategoryKind k) {
        BrowserNode n; n.type = BrowserNode::Category; n.category = k; return n;
    };

    {
        FakeBrowserClient fc; std::string err;
        auto out = navidrome::fetchChildren(fc, artist(""), err);
        check(fc.calls.size() == 1 && fc.calls[0] == "getAlbumsForArtist:",
              "an unpinned artist fetches albums with an empty scope");
        check(out.size() == 1 && out[0]->type == BrowserNode::Album, "-> album nodes");
    }
    {
        FakeBrowserClient fc; std::string err;
        navidrome::fetchChildren(fc, artist("2"), err);
        check(fc.calls[0] == "getAlbumsForArtist:2",
              "an artist under a Library node pins the album scope to that library");
    }
    {
        FakeBrowserClient fc; std::string err;
        BrowserNode lib; lib.type = BrowserNode::Library; lib.id = "2";
        auto out = navidrome::fetchChildren(fc, lib, err);
        check(fc.calls[0] == "getArtistsForLibrary" &&
              out.size() == 1 && out[0]->type == BrowserNode::Artist &&
              out[0]->libraryId == "2",
              "a Library node fetches its artists and pins them to itself");
    }
    {
        FakeBrowserClient fc; std::string err;
        BrowserNode g; g.type = BrowserNode::Genre; g.id = "Rock";
        navidrome::fetchChildren(fc, g, err);
        check(fc.calls[0] == "getSongsForGenre:500",
              "genre expansion asks for up to 500 songs in one request");
    }
    {
        FakeBrowserClient fc; std::string err;
        auto out = navidrome::fetchChildren(fc, cat(BrowserNode::CatBookmarks), err);
        check(fc.calls[0] == "getBookmarks" && out.size() == 1 &&
              out[0]->type == BrowserNode::Song && out[0]->bookmarkPositionMs == 5000,
              "the Bookmarks category yields song nodes carrying the resume position");
    }
    {
        FakeBrowserClient fc; std::string err;
        navidrome::fetchChildren(fc, cat(BrowserNode::CatMostPlayed), err);
        check(fc.calls[0] == std::string("getAlbumList:frequent:100"),
              "Most Played maps to getAlbumList2 frequent, 100 rows");
    }
    {
        FakeBrowserClient fc; std::string err;
        navidrome::fetchChildren(fc, cat(BrowserNode::CatRecentlyAdded), err);
        check(fc.calls[0] == std::string("getAlbumList:newest:100"),
              "Recently Added maps to getAlbumList2 newest");
    }
    {
        FakeBrowserClient fc; fc.error = "net";
        std::string err;
        auto out = navidrome::fetchChildren(fc, cat(BrowserNode::CatStarred), err);
        check(err == "net" && out.empty(),
              "a failed child fetch clears the result and surfaces the error");
    }

    // --- collectSongsDeep: recurses through the tree, reuses loaded children ---
    {
        FakeBrowserClient fc;
        auto artistNode = std::make_shared<BrowserNode>();
        artistNode->type = BrowserNode::Artist; artistNode->id = "ar1";
        std::vector<navidrome::BrowserNodePtr> songs;
        navidrome::collectSongsDeep(fc, artistNode, songs);
        // artist -> (fetch) album -> (fetch) song
        check(songs.size() == 1 && songs[0]->type == BrowserNode::Song,
              "collectSongsDeep walks artist -> album -> song via fetches");

        FakeBrowserClient fc2;
        auto preloaded = std::make_shared<BrowserNode>();
        preloaded->type = BrowserNode::Album; preloaded->childrenLoaded = true;
        preloaded->children = { navidrome::makeSongNode([]{
            navidrome::Song s; s.id = "x"; s.title = "cached"; return s; }()) };
        songs.clear();
        navidrome::collectSongsDeep(fc2, preloaded, songs);
        check(songs.size() == 1 && songs[0]->id == "x" && fc2.calls.empty(),
              "an already-loaded node is walked from its cached children, no fetch");

        auto ids = navidrome::collectSongIdsDeep(fc, { artistNode });
        check(ids.size() == 1 && ids[0] == "s1",
              "collectSongIdsDeep returns the non-empty song ids");
    }

    // --- syncBrowserNodesToPlaylists: Song filter + RatingUpdate build ---
    {
        navidrome::Song s1; s1.id = "s1"; s1.rating = 4; s1.starred = true;
        navidrome::Song s2; s2.id = "";   s2.rating = 2;      // no id -> skipped
        std::vector<navidrome::BrowserNodePtr> mixed = {
            navidrome::makeSongNode(s1),
            navidrome::makeSongNode(s2),
            navidrome::makeAlbumNode([]{ navidrome::Album a; a.id = "al"; return a; }()),
            navidrome::makeCategoryNode(BrowserNode::CatStarred, "x"),
            nullptr,
        };
        navidrome::g_lastRatingSync.clear();
        navidrome::syncBrowserNodesToPlaylists(mixed);
        check(navidrome::g_lastRatingSync.size() == 1 &&
              navidrome::g_lastRatingSync[0].songId == "s1" &&
              navidrome::g_lastRatingSync[0].rating == 4 &&
              navidrome::g_lastRatingSync[0].starred,
              "syncBrowserNodesToPlaylists forwards only id-bearing Song nodes");
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
    testScrobbleThreshold();
    testMusicFolderFilter();
    testRawQueryParam();
    testTrackURICodec();
    testBrowserModel();
    testBrowserFetchDispatch();
    testMd5KnownAnswers();
    testCrossParserParity();
    testErrorModel();
    testJson();
    testSubsonicParsers();
    testRetryPolicy();
    testScrobbleTracker();
    testSessionEnv();
    testFanOutMerge();
    testAlbumArtistFilter();
    testPrefsOptions();
    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All MediaEnrichment tests passed\n";
    return 0;
}
