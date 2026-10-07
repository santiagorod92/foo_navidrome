#pragma once
// Shared by BrowserFetchTests.cpp and BrowserActionsTests.cpp.
#include "../src/core/NavidromeBrowserModel.h"

#include <set>
#include <string>
#include <vector>

// A recording IBrowserClient: every call appends its name to `calls` and
// returns one canned item so the dispatch can be asserted without a network.
struct FakeBrowserClient : navidrome::IBrowserClient {
    std::vector<std::string> calls;
    std::string error;                 // set non-empty to simulate a failure
    std::vector<std::string> groupIds; // set 2+ to exercise the library grouping
    navidrome::Lyrics lyrics;          // what getLyrics returns on success

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
    std::vector<navidrome::PodcastChannel> getPodcastChannels(std::string& e) override {
        navidrome::PodcastChannel c; c.id = "ch1"; c.title = "Channel"; c.description = "A show";
        return one("getPodcastChannels", e, c);
    }
    std::vector<navidrome::PodcastEpisode> getPodcastEpisodes(const std::string& channelId,
                                                               std::string& e) override {
        calls.push_back("getPodcastEpisodes:" + channelId);
        e = error;
        if (!error.empty()) return {};
        navidrome::PodcastEpisode done, pending;
        done.id = "ep1"; done.streamId = "s8"; done.title = "Done episode"; done.status = "completed";
        pending.id = "ep2"; pending.title = "Pending episode"; pending.status = "downloading";
        return { done, pending };
    }
    std::vector<navidrome::NowPlayingEntry> getNowPlaying(std::string& e) override {
        navidrome::NowPlayingEntry np;
        np.song.id = "s9"; np.song.title = "Live"; np.username = "alice"; np.minutesAgo = 3;
        return one("getNowPlaying", e, np);
    }
    std::vector<navidrome::Song> getSimilarSongs(const std::string& id, int count,
                                                 std::string& e) override {
        calls.push_back("getSimilarSongs:" + id + ":" + std::to_string(count));
        e = error;
        if (!error.empty()) return {};
        navidrome::Song s; s.id = "s6"; s.title = "Similar"; return { s };
    }
    std::vector<navidrome::Song> getAllSongs(std::string& e) override {
        calls.push_back("getAllSongs");
        e = error;
        if (!error.empty()) return {};
        navidrome::Song a; a.id = "all1"; a.title = "One";
        navidrome::Song b; b.id = "all2"; b.title = "Two";
        return { a, b };
    }
    std::vector<navidrome::Song> getRandomSongs(int count, std::string& e) override {
        calls.push_back("getRandomSongs:" + std::to_string(count));
        e = error;
        if (!error.empty()) return {};
        navidrome::Song s; s.id = "s7"; s.title = "Random"; return { s };
    }
    navidrome::ArtistInfo getArtistInfo(const std::string& id, std::string& e) override {
        calls.push_back("getArtistInfo:" + id);
        e = error;
        if (!error.empty()) return {};
        navidrome::ArtistInfo info;
        info.biography = "Bio";
        navidrome::Artist similar; similar.id = "ar-similar"; similar.name = "Similar";
        info.similarArtists = { similar };
        return info;
    }
    std::vector<navidrome::Song> getTopSongs(const std::string& artistName, int count,
                                             std::string& e) override {
        calls.push_back("getTopSongs:" + artistName + ":" + std::to_string(count));
        e = error;
        if (!error.empty()) return {};
        navidrome::Song s; s.id = "s-top"; s.title = "Top"; return { s };
    }
    navidrome::Lyrics getLyrics(const std::string& id, const std::string& artist,
                                const std::string& title, std::string& e) override {
        calls.push_back("getLyrics:" + id + ":" + artist + ":" + title);
        e = error;
        if (!error.empty()) return {};
        return lyrics;
    }
    std::vector<std::string> groupingLibraryIds() override {
        calls.push_back("groupingLibraryIds");
        return groupIds;
    }
    std::vector<navidrome::MusicFolder> musicFolders() override {
        calls.push_back("musicFolders");
        return { {"1", "Music"}, {"2", "Podcasts"} };
    }

    // set of ids that fail setStarred/setRating, to exercise the partial-failure path
    std::set<std::string> failIds;

    bool setStarred(bool starred, const std::string& id, navidrome::StarKind kind,
                    std::string& e) override {
        calls.push_back("setStarred:" + id + ":" + std::to_string((int)kind) + ":" +
                        (starred ? "1" : "0"));
        if (failIds.count(id)) { e = "failed to star " + id; return false; }
        e.clear();
        return true;
    }
    bool setRating(int stars, const std::string& id, std::string& e) override {
        calls.push_back("setRating:" + id + ":" + std::to_string(stars));
        if (failIds.count(id)) { e = "failed to rate " + id; return false; }
        e.clear();
        return true;
    }
    bool getSong(const std::string& id, navidrome::Song& out, std::string& e) override {
        calls.push_back("getSong:" + id);
        if (failIds.count(id)) { e = "song " + id + " not found"; return false; }
        e.clear();
        out = {};
        out.id = id; out.title = "Song " + id; out.artist = "Artist"; out.suffix = "flac";
        return true;
    }
};
