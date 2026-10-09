#include "TestHarness.h"
#include "../src/core/SubsonicTypes.h"

#include <string>
#include <vector>

namespace {

TEST_CASE(testStarKindAndAlbumListType) {
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

TEST_CASE(testParseHeaderLines) {
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

TEST_CASE(testPercentDecodeEdgeCases) {
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

TEST_CASE(testPlaylistChunkSize) {
    check(navidrome::kPlaylistChunkSize == 50,
        "playlist mutations are chunked at 50 ids per request");
}

TEST_CASE(testTranscodeParams) {
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

TEST_CASE(testFileNames) {
    using navidrome::sanitizeFileName;
    check(sanitizeFileName("AC/DC: Back in Black?") == "AC_DC_ Back in Black_",
        "path and reserved characters are replaced");
    check(sanitizeFileName("trailing dots...") == "trailing dots",
        "trailing dots are trimmed (Windows rejects them)");
    check(sanitizeFileName("   ") == "untitled",
        "an all-trimmed name falls back to a placeholder");
    check(sanitizeFileName("中文 title") == "中文 title",
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

TEST_CASE(testTrackURICodec) {
    using navidrome::TrackURI;
    using navidrome::buildTrackURI;
    using navidrome::parseTrackURI;

    check(buildTrackURI(TrackURI{}).empty(), "an empty id builds no URI");

    TrackURI bare;
    bare.id = "abc123";
    check(buildTrackURI(bare) == "navidrome://track/abc123",
        "an id-only track has no query string");

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

    TrackURI minimal;
    minimal.id = "x";
    minimal.title = "T";
    const std::string mUri = buildTrackURI(minimal);
    check(mUri == "navidrome://track/x?title=T", "only set fields appear in the query");
    check(mUri.find("rating=") == std::string::npos, "an unset rating is absent, not rating=0");
    check(mUri.find("starred=") == std::string::npos, "an unset star is absent");

    check(parseTrackURI("https://server/music.mp3?title=x").id.empty(),
        "a non-navidrome URI yields no id");
    check(parseTrackURI("navidrome://track/").id.empty(),
        "the bare prefix yields no id");

    const TrackURI legacy = parseTrackURI("navidrome://track/old?title=Song&artist=Nine");
    check(legacy.id == "old" && legacy.title == "Song" && legacy.artist == "Nine",
        "a legacy URI's known fields parse");
    check(legacy.rating == 0 && !legacy.starred && legacy.albumId.empty(),
        "a legacy URI's absent fields stay at their defaults");

    const TrackURI fwd = parseTrackURI("navidrome://track/y?title=Z&future=1&rating=2");
    check(fwd.title == "Z" && fwd.rating == 2, "an unknown key is skipped, known keys still read");
}

TEST_CASE(testScrobbleThreshold) {
    using navidrome::scrobbleSubmitThreshold;
    check(scrobbleSubmitThreshold(200.0) == 100.0, "short track: submit at half length");
    check(scrobbleSubmitThreshold(600.0) == 240.0, "long track: submit is capped at 4 min");
    check(scrobbleSubmitThreshold(480.0) == 240.0, "exactly 8 min: half == cap");
    check(scrobbleSubmitThreshold(0.0) == 240.0, "unknown length falls back to the cap");
    check(scrobbleSubmitThreshold(-1.0) == 240.0, "negative length falls back to the cap");
}

TEST_CASE(testMusicFolderFilter) {
    using navidrome::MusicFolder;
    using navidrome::parseMusicFolderIds;
    using navidrome::joinMusicFolderIds;
    using navidrome::effectiveMusicFolderIds;

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

    check((effectiveMusicFolderIds(true, "1", two) == std::vector<std::string>{"1"}),
        "one-of-two selected -> fan out over that id");
    check((effectiveMusicFolderIds(true, "3,1", three) ==
           std::vector<std::string>{"1", "3"}),
        "fan-out list follows server order, not selection order");
    check((effectiveMusicFolderIds(true, "2,9", three) ==
           std::vector<std::string>{"2"}),
        "a stale id in an otherwise valid selection is dropped");
}

TEST_CASE(testRawQueryParam) {
    using navidrome::rawQueryParam;
    const std::string legacy =
        "https://s/rest/stream.view?id=song%2F1&coverArt=art%2F9&u=me";
    check(rawQueryParam(legacy, "coverArt") == "art/9",
        "a param is read out of a plain HTTP url and percent-decoded");
    check(rawQueryParam(legacy, "id") == "song/1", "the first param is read");
    check(rawQueryParam(legacy, "size").empty(), "an absent param reads empty");
    check(rawQueryParam("navidrome://track/x", "id").empty(),
        "no query string means empty, not a crash");
    check(rawQueryParam("x?guid=abc&id=real", "id") == "real",
        "a param name is only matched at a pair boundary");
}

TEST_CASE(testQueryParams) {
    using navidrome::queryParamFromURI;
    const std::string uri =
        "navidrome://track/abc?title=Song&album=Live%20Set&rating=4&starred=1"
        "&albumId=alb%2F42";

    check(queryParamFromURI(uri, "rating") == "4", "a middle parameter is read");
    check(queryParamFromURI(uri, "albumId") == "alb/42",
        "the last parameter is read and percent-decoded");
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
}
