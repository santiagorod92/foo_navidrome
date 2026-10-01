// Unit tests: Shared client policy in SubsonicTypes.h — the error model, multi-library
// fan-out/merge, retry policy, scrobble tracker, broken-track registry,
// prefs option lists and the session-environment summary.
#include "TestHarness.h"
#include "../src/core/SubsonicTypes.h"

#include <string>
#include <vector>

namespace {

TEST_CASE(testErrorModel) {
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

TEST_CASE(testFanOutMerge) {
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

TEST_CASE(testAlbumArtistFilter) {
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

TEST_CASE(testPrefsOptions) {
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

TEST_CASE(testRetryPolicy) {
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

TEST_CASE(testScrobbleTracker) {
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

TEST_CASE(testBrokenTrackRegistry) {
    // Fresh instance per case — not the process-wide brokenTrackRegistry()
    // singleton, so cases can't bleed into each other.
    navidrome::BrokenTrackRegistry r;
    check(!r.isBroken("s1"), "nothing is broken before markBroken");

    r.markBroken("s1");
    check(r.isBroken("s1"), "marked id reads back as broken");
    check(!r.isBroken("s2"), "an unmarked id stays unaffected");

    r.markBroken("s1");  // idempotent
    check(r.isBroken("s1"), "marking the same id twice is a no-op, not an error");

    r.markBroken("");
    check(!r.isBroken(""), "an empty id is never markable/broken (not-one-of-ours sentinel)");
}

TEST_CASE(testSessionEnv) {
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

} // namespace
