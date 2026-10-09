#include "TestHarness.h"
#include "../src/core/SubsonicTypes.h"

#include <string>
#include <vector>

namespace {

TEST_CASE(testJson) {
    using navidrome::json::Value;
    std::string err;

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

    Value s = navidrome::json::parse("\"a\\\"b\\\\c\\/d\\n\"", err);
    check(err.empty() && s.type() == Value::String && s.asString() == "a\"b\\c/d\n",
        "decodes \\\" \\\\ \\/ \\n escapes");
    check(navidrome::json::parse("\"caf\\u00e9\"", err).asString() == "caf\xC3\xA9" && err.empty(),
        "\\u00e9 decodes to 2-byte UTF-8");
    check(navidrome::json::parse("\"\\uD83D\\uDE00\"", err).asString() == "\xF0\x9F\x98\x80" && err.empty(),
        "a surrogate pair decodes to 4-byte UTF-8");

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

    Value arr = navidrome::json::parse("{\"x\":[{\"id\":1},{\"id\":2}]}", err);
    check(arr["x"].items().size() == 2, "items() over a real array");
    Value one = navidrome::json::parse("{\"x\":{\"id\":9}}", err);
    check(one["x"].items().size() == 1 && (*one["x"].items()[0])["id"].asNumber() == 9.0,
        "items() wraps a collapsed single object");
    check(navidrome::json::parse("{}", err)["x"].items().empty(),
        "items() over a missing field is empty");

    navidrome::json::parse("{\"a\":}", err);
    check(!err.empty(), "reports an error on a missing value");
    navidrome::json::parse("[1,2", err);
    check(!err.empty(), "reports an error on an unterminated array");
    navidrome::json::parse("{\"a\":1} trailing", err);
    check(!err.empty(), "rejects trailing content after the value");
    navidrome::json::parse("\"unterminated", err);
    check(!err.empty(), "reports an error on an unterminated string");
}

TEST_CASE(testSubsonicParsers) {
    std::string err;
    auto parse = [&](const std::string& body) {
        return navidrome::json::parse(body, err);
    };

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

    navidrome::Album al = navidrome::parseAlbum(parse(
        "{\"id\":\"al1\",\"name\":\"Rec\",\"artist\":\"A\",\"artistId\":\"ar1\","
        "\"coverArt\":\"c\",\"year\":1999,\"songCount\":10,\"starred\":\"x\"}"));
    check(al.id == "al1" && al.name == "Rec" && al.year == 1999 && al.songCount == 10 && al.starred,
        "parseAlbum fields");
    check(navidrome::parseAlbum(parse("{}")).name == "Unknown Album",
        "parseAlbum name default");

    navidrome::Artist ar = navidrome::parseArtist(parse(
        "{\"id\":\"ar1\",\"name\":\"Band\",\"albumCount\":7}"));
    check(ar.id == "ar1" && ar.name == "Band" && ar.albumCount == 7, "parseArtist fields");
    check(navidrome::parseArtist(parse("{}")).name == "Unknown Artist",
        "parseArtist name default");

    navidrome::Playlist pl = navidrome::parsePlaylist(parse(
        "{\"id\":9,\"name\":\"Mix\",\"owner\":\"me\",\"songCount\":4,\"duration\":88.0}"));
    check(pl.id == "9" && pl.name == "Mix" && pl.owner == "me" && pl.songCount == 4,
        "parsePlaylist fields, numeric id coerced");

    navidrome::Genre g = navidrome::parseGenre(parse(
        "{\"value\":\"Jazz\",\"songCount\":12,\"albumCount\":3}"));
    check(g.name == "Jazz" && g.songCount == 12 && g.albumCount == 3,
        "parseGenre reads the genre string from \"value\"");

    navidrome::MusicFolder mf = navidrome::parseMusicFolder(parse(
        "{\"id\":1,\"name\":\"Music\"}"));
    check(mf.id == "1" && mf.name == "Music", "parseMusicFolder coerces a numeric id");

    navidrome::RadioStation st = navidrome::parseRadioStation(parse(
        "{\"id\":\"r1\",\"name\":\"Radio\",\"streamUrl\":\"http://s/\",\"homePageUrl\":\"http://h/\"}"));
    check(st.id == "r1" && st.streamUrl == "http://s/" && st.homePageUrl == "http://h/",
        "parseRadioStation fields");
    check(navidrome::parseRadioStation(parse("{\"id\":\"r2\"}")).name == "Unnamed station",
        "parseRadioStation name default");

    navidrome::Bookmark bm;
    bool okBm = navidrome::parseBookmark(parse(
        "{\"position\":45000,\"comment\":\"resume\",\"entry\":{\"id\":\"t9\",\"title\":\"X\"}}"), bm);
    check(okBm && bm.song.id == "t9" && bm.positionMs == 45000.0 && bm.comment == "resume",
        "parseBookmark extracts the entry song and ms position");
    check(!navidrome::parseBookmark(parse("{\"position\":1}"), bm),
        "parseBookmark returns false with no entry");

    navidrome::ScanStatus ss = navidrome::parseScanStatus(parse(
        "{\"scanStatus\":{\"scanning\":true,\"count\":1234}}"));
    check(ss.scanning && ss.count == 1234, "parseScanStatus reads scanning + count");

    navidrome::ArtistInfo ai = navidrome::parseArtistInfo2(parse(
        "{\"artistInfo2\":{\"biography\":\"A great <a href=\\\"x\\\">band</a> &amp; friends\","
        "\"lastFmUrl\":\"http://last.fm/band\",\"musicBrainzId\":\"mb1\","
        "\"similarArtist\":[{\"id\":\"ar2\",\"name\":\"Other Band\"}]}}"));
    check(ai.lastFmUrl == "http://last.fm/band" && ai.musicBrainzId == "mb1",
        "parseArtistInfo2 reads top-level fields");
    check(ai.similarArtists.size() == 1 && ai.similarArtists[0].id == "ar2" &&
          ai.similarArtists[0].name == "Other Band",
        "parseArtistInfo2 reuses parseArtist for similarArtist entries");
    check(ai.biography == "A great <a href=\"x\">band</a> &amp; friends",
        "parseArtistInfo2 keeps the biography raw (stripping happens at display time)");

    navidrome::ArtistInfo aiEmpty = navidrome::parseArtistInfo2(parse("{}"));
    check(aiEmpty.biography.empty() && aiEmpty.similarArtists.empty(),
        "parseArtistInfo2 defaults on a missing artistInfo2 object");

    check(navidrome::stripHtmlTags("A great <a href=\"x\">band</a> &amp; friends") ==
          "A great band & friends",
        "stripHtmlTags drops tags and unescapes entities");
    check(navidrome::stripHtmlTags("Line1<br>Line2   trailing  ") == "Line1 Line2 trailing",
        "stripHtmlTags collapses whitespace left by stripped tags and trims trailing spaces");

    navidrome::ArtistInfo bioInfo;
    bioInfo.biography = "A <b>great</b> band";
    bioInfo.lastFmUrl  = "http://last.fm/band";
    check(navidrome::formatArtistBiography(bioInfo) ==
          "A great band\n\nhttp://last.fm/band",
        "formatArtistBiography strips markup and appends the last.fm link");
    check(navidrome::formatArtistBiography(navidrome::ArtistInfo{}) == "No biography available.",
        "formatArtistBiography falls back when the server has no biography");

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
}
