#include "TestHarness.h"
#include "../src/core/NavidromeBrowserModel.h"

#include <string>
#include <vector>

namespace {

navidrome::Song songFrom(const std::string& body) {
    std::string err;
    auto v = navidrome::json::parse(body, err);
    check(err.empty(), "test song JSON parses");
    return navidrome::parseSong(v);
}

bool hasField(const std::vector<std::pair<std::string, std::string>>& fields,
              const std::string& key, const std::string& value) {
    for (const auto& f : fields)
        if (f.first == key && f.second == value) return true;
    return false;
}

TEST_CASE(testTrackTagsParse) {
    const navidrome::Song s = songFrom(R"j({"id":"s1","title":"T","discNumber":2,"bpm":128,
        "musicBrainzId":"mb-rec","displayAlbumArtist":"Various Artists","displayComposer":"Bach",
        "genre":"Rock","genres":[{"name":"Rock"},{"name":"Prog"}],
        "replayGain":{"trackGain":-7.5,"albumGain":-6.25,"trackPeak":0.98,"albumPeak":1.0}})j");
    const navidrome::TrackTags& t = s.tags;
    check(t.discNumber == 2 && t.bpm == 128 && t.musicBrainzId == "mb-rec",
          "disc, bpm and MusicBrainz id come from the song");
    check(t.albumArtist == "Various Artists" && t.composer == "Bach",
          "displayAlbumArtist / displayComposer feed album artist / composer");
    check(t.genres.size() == 2 && t.genres[0] == "Rock" && t.genres[1] == "Prog",
          "OpenSubsonic genres[] wins over the single legacy genre");
    const navidrome::ReplayGain& rg = t.replayGain;
    check(rg.hasTrackGain && rg.trackGain == -7.5 && rg.hasAlbumGain && rg.albumGain == -6.25 &&
          rg.hasTrackPeak && rg.trackPeak == 0.98 && rg.hasAlbumPeak && rg.albumPeak == 1.0,
          "all four ReplayGain values are read");

    const navidrome::Song legacy = songFrom(R"j({"id":"s2","genre":"Jazz",
        "albumArtists":[{"id":"a","name":"A"},{"id":"b","name":"B"}]})j");
    check(legacy.tags.genres.size() == 1 && legacy.tags.genres[0] == "Jazz",
          "plain Subsonic genre is used when genres[] is absent");
    check(legacy.tags.albumArtist == "A, B",
          "albumArtists[] names are joined when displayAlbumArtist is absent");
    check(legacy.tags.replayGain.empty() && legacy.tags.discNumber == 0,
          "missing fields stay empty");

    const navidrome::Song zeros = songFrom(R"j({"id":"s3",
        "replayGain":{"trackGain":0,"albumGain":0,"trackPeak":0,"albumPeak":0}})j");
    check(zeros.tags.replayGain.empty(),
          "an all-zero ReplayGain block (untagged file on older servers) counts as none");

    const navidrome::Song gainOnly = songFrom(R"j({"id":"s4",
        "replayGain":{"trackGain":-3,"trackPeak":0}})j");
    check(gainOnly.tags.replayGain.hasTrackGain && !gainOnly.tags.replayGain.hasTrackPeak,
          "a zero peak is dropped but a real gain is kept");
}

TEST_CASE(testTrackTagsUriRoundTrip) {
    navidrome::TrackURI in;
    in.id = "abc";
    in.title = "Song";
    in.tags.albumArtist = "Simon & Garfunkel";
    in.tags.composer = "Paul Simon";
    in.tags.genres = { "Folk", "Rock; Pop" };
    in.tags.discNumber = 2;
    in.tags.bpm = 90;
    in.tags.musicBrainzId = "1234-abcd";
    in.tags.replayGain.trackGain = -8.12;  in.tags.replayGain.hasTrackGain = true;
    in.tags.replayGain.albumPeak = 0.995;  in.tags.replayGain.hasAlbumPeak = true;

    const std::string uri = navidrome::buildTrackURI(in);
    check(uri.find("albumArtist=Simon%20%26%20Garfunkel") != std::string::npos,
          "album artist is percent-encoded into the URI");
    check(uri.find("rgAlbumGain") == std::string::npos && uri.find("rgTrackPeak") == std::string::npos,
          "absent ReplayGain values are not written");

    const navidrome::TrackURI out = navidrome::parseTrackURI(uri);
    check(out.tags.albumArtist == in.tags.albumArtist && out.tags.composer == in.tags.composer &&
          out.tags.musicBrainzId == in.tags.musicBrainzId,
          "string tags round-trip");
    check(out.tags.genres == in.tags.genres, "repeated genre params round-trip in order");
    check(out.tags.discNumber == 2 && out.tags.bpm == 90, "disc and bpm round-trip");
    check(out.tags.replayGain.hasTrackGain && out.tags.replayGain.trackGain == -8.12 &&
          out.tags.replayGain.hasAlbumPeak && out.tags.replayGain.albumPeak == 0.995 &&
          !out.tags.replayGain.hasAlbumGain && !out.tags.replayGain.hasTrackPeak,
          "ReplayGain values and their presence round-trip");

    const navidrome::TrackURI old = navidrome::parseTrackURI("navidrome://track/x?title=T&artist=A");
    check(old.tags.genres.empty() && old.tags.replayGain.empty() && old.tags.albumArtist.empty(),
          "a URI written before tags existed parses with no tags");
}

TEST_CASE(testTrackMetaFields) {
    navidrome::TrackURI t;
    t.id = "x";
    t.title = "Title";
    t.artist = "Artist";
    t.track = 3;
    t.year = 1999;
    t.tags.albumArtist = "AA";
    t.tags.genres = { "G1", "G2" };
    t.tags.discNumber = 1;
    t.tags.musicBrainzId = "mb";
    const auto f = navidrome::trackMetaFields(t);
    check(hasField(f, "title", "Title") && hasField(f, "artist", "Artist") &&
          hasField(f, "tracknumber", "3") && hasField(f, "date", "1999"),
          "base fields are exported");
    check(hasField(f, "album artist", "AA") && hasField(f, "discnumber", "1") &&
          hasField(f, navidrome::kMusicBrainzTrackIdTag, "mb"),
          "album artist, disc and MusicBrainz id use foobar2000's field names");
    check(hasField(f, "genre", "G1") && hasField(f, "genre", "G2"),
          "each genre is its own value of the multi-value genre field");
    check(!hasField(f, "bpm", "0") && !hasField(f, "composer", ""),
          "unset tags are not exported");

    navidrome::Song s;
    s.id = "n1";
    s.title = "From node";
    s.tags.composer = "C";
    s.tags.replayGain.trackGain = -1; s.tags.replayGain.hasTrackGain = true;
    const navidrome::TrackURI fromNode = navidrome::trackURIFromNode(*navidrome::makeSongNode(s));
    check(fromNode.id == "n1" && fromNode.title == "From node" && fromNode.tags.composer == "C" &&
          fromNode.tags.replayGain.hasTrackGain,
          "song node → URI keeps the tags");
}
}
