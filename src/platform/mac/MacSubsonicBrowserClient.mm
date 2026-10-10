#import "MacSubsonicBrowserClient.h"
#import "SubsonicClient.h"
#include "../../core/NavidromeLibraryPlatform.h"
#include "../../core/NavidromeDebugLog.h"
#import <Foundation/Foundation.h>
#include <string>
#include <utility>
#include <vector>

namespace navidrome {
    extern cfg_string cfg_browser_hidden_categories;
}

namespace {

std::string str(NSString *x) { return x ? std::string(x.UTF8String) : std::string(); }

std::string errString(NSError *err) {
    if (!err) return std::string();
    NSString *d = err.localizedDescription;
    return d ? std::string(d.UTF8String) : std::string("Unknown error");
}

navidrome::Artist conv(SubsonicArtist *a) {
    navidrome::Artist r;
    r.id         = str(a.artistId);
    r.name       = str(a.name);
    r.coverArtId = str(a.coverArtId);
    r.albumCount = (int)a.albumCount;
    r.starred    = a.starred;
    return r;
}

navidrome::Album conv(SubsonicAlbum *a) {
    navidrome::Album r;
    r.id         = str(a.albumId);
    r.name       = str(a.name);
    r.artist     = str(a.artist);
    r.artistId   = str(a.artistId);
    r.coverArtId = str(a.coverArtId);
    r.year       = (int)a.year;
    r.songCount  = (int)a.songCount;
    r.starred    = a.starred;
    return r;
}

navidrome::Song conv(SubsonicSong *x) {
    navidrome::Song r;
    r.id         = str(x.songId);
    r.title      = str(x.title);
    r.artist     = str(x.artist);
    r.artistId   = str(x.artistId);
    r.album      = str(x.album);
    r.albumId    = str(x.albumId);
    r.coverArtId = str(x.coverArtId);
    r.suffix     = str(x.suffix);
    r.track      = (int)x.track;
    r.year       = (int)x.year;
    r.duration   = x.duration;
    r.starred    = x.starred;
    r.rating     = (int)x.rating;
    return r;
}

navidrome::Playlist conv(SubsonicPlaylist *p) {
    navidrome::Playlist r;
    r.id        = str(p.playlistId);
    r.name      = str(p.name);
    r.owner     = str(p.owner);
    r.songCount = (int)p.songCount;
    r.duration  = p.duration;
    return r;
}

navidrome::Genre conv(SubsonicGenre *g) {
    navidrome::Genre r;
    r.name       = str(g.name);
    r.songCount  = (int)g.songCount;
    r.albumCount = (int)g.albumCount;
    return r;
}

navidrome::RadioStation conv(SubsonicRadioStation *x) {
    navidrome::RadioStation r;
    r.id          = str(x.stationId);
    r.name        = str(x.name);
    r.streamUrl   = str(x.streamUrl);
    r.homePageUrl = str(x.homePageUrl);
    return r;
}

navidrome::Bookmark conv(SubsonicBookmark *b) {
    navidrome::Bookmark r;
    r.song       = conv(b.song);
    r.positionMs = b.positionMs;
    r.comment    = str(b.comment);
    return r;
}

navidrome::MusicFolder conv(SubsonicMusicFolder *f) {
    navidrome::MusicFolder r;
    r.id   = str(f.folderId);
    r.name = str(f.name);
    return r;
}

navidrome::PodcastChannel conv(SubsonicPodcastChannel *c) {
    navidrome::PodcastChannel r;
    r.id           = str(c.channelId);
    r.url          = str(c.url);
    r.title        = str(c.title);
    r.description  = str(c.channelDescription);
    r.status       = str(c.status);
    r.errorMessage = str(c.errorMessage);
    return r;
}

navidrome::PodcastEpisode conv(SubsonicPodcastEpisode *e) {
    navidrome::PodcastEpisode r;
    r.id          = str(e.episodeId);
    r.streamId    = str(e.streamId);
    r.channelId   = str(e.channelId);
    r.title       = str(e.title);
    r.description = str(e.episodeDescription);
    r.status      = str(e.status);
    r.duration    = e.duration;
    return r;
}

navidrome::NowPlayingEntry conv(SubsonicNowPlayingEntry *x) {
    navidrome::NowPlayingEntry r;
    r.song       = conv(x.song);
    r.username   = str(x.username);
    r.minutesAgo = (int)x.minutesAgo;
    return r;
}

navidrome::ArtistInfo conv(SubsonicArtistInfo *x) {
    navidrome::ArtistInfo r;
    r.biography      = str(x.biography);
    r.musicBrainzId  = str(x.musicBrainzId);
    r.lastFmUrl       = str(x.lastFmUrl);
    r.smallImageUrl   = str(x.smallImageUrl);
    r.mediumImageUrl  = str(x.mediumImageUrl);
    r.largeImageUrl   = str(x.largeImageUrl);
    for (SubsonicArtist *a in x.similarArtists) r.similarArtists.push_back(conv(a));
    return r;
}

template <class ObjC, class T = decltype(conv(std::declval<ObjC *>()))>
std::vector<T> mapArr(NSArray<ObjC *> *arr) {
    std::vector<T> v;
    v.reserve(arr.count);
    for (ObjC *x in arr) v.push_back(conv(x));
    return v;
}

struct MacBrowserClient final : navidrome::IBrowserClient {
    SubsonicClient *client = SubsonicClient.sharedClient;

    std::vector<navidrome::Artist> getArtists(std::string& e) override {
        NSError *err = nil;
        auto v = mapArr<SubsonicArtist>([client getArtistsWithError:&err]);
        e = errString(err);
        return v;
    }
    std::vector<navidrome::Artist> getArtistsForLibrary(const std::string& id,
                                                        std::string& e) override {
        NSError *err = nil;
        auto v = mapArr<SubsonicArtist>([client getArtistsForLibrary:@(id.c_str()) error:&err]);
        e = errString(err);
        return v;
    }
    std::vector<navidrome::Album> getAlbumsForArtist(const std::string& id,
                                                     const std::string& scope,
                                                     std::string& e) override {
        NSError *err = nil;
        NSString *scopeLib = scope.empty() ? nil : @(scope.c_str());
        auto v = mapArr<SubsonicAlbum>([client getAlbumsForArtist:@(id.c_str())
                                                            error:&err
                                                     scopeLibrary:scopeLib]);
        e = errString(err);
        return v;
    }
    std::vector<navidrome::Song> getSongsForAlbum(const std::string& id,
                                                  std::string& e) override {
        NSError *err = nil;
        auto v = mapArr<SubsonicSong>([client getSongsForAlbum:@(id.c_str()) error:&err]);
        e = errString(err);
        return v;
    }
    std::vector<navidrome::Song> getPlaylistSongs(const std::string& id,
                                                  std::string& e) override {
        NSError *err = nil;
        auto v = mapArr<SubsonicSong>([client getPlaylistSongs:@(id.c_str()) error:&err]);
        e = errString(err);
        return v;
    }
    std::vector<navidrome::Song> getSongsForGenre(const std::string& g, int count,
                                                  std::string& e) override {
        NSError *err = nil;
        auto v = mapArr<SubsonicSong>([client getSongsForGenre:@(g.c_str()) count:count error:&err]);
        e = errString(err);
        return v;
    }
    std::vector<navidrome::Song> getStarredSongs(std::string& e) override {
        NSError *err = nil;
        auto v = mapArr<SubsonicSong>([client getStarredSongsWithError:&err]);
        e = errString(err);
        return v;
    }
    std::vector<navidrome::Genre> getGenres(std::string& e) override {
        NSError *err = nil;
        auto v = mapArr<SubsonicGenre>([client getGenresWithError:&err]);
        e = errString(err);
        return v;
    }
    std::vector<navidrome::Playlist> getPlaylists(std::string& e) override {
        NSError *err = nil;
        auto v = mapArr<SubsonicPlaylist>([client getPlaylistsWithError:&err]);
        e = errString(err);
        return v;
    }
    std::vector<navidrome::Album> getAlbumList(navidrome::AlbumListType t, int size,
                                               std::string& e) override {
        NSError *err = nil;
        NSString *type = @(navidrome::albumListTypeName(t));
        auto v = mapArr<SubsonicAlbum>([client getAlbumListOfType:type size:size error:&err]);
        e = errString(err);
        return v;
    }
    std::vector<navidrome::RadioStation> getRadioStations(std::string& e) override {
        NSError *err = nil;
        auto v = mapArr<SubsonicRadioStation>([client getRadioStationsWithError:&err]);
        e = errString(err);
        return v;
    }
    std::vector<navidrome::Bookmark> getBookmarks(std::string& e) override {
        NSError *err = nil;
        auto v = mapArr<SubsonicBookmark>([client getBookmarksWithError:&err]);
        e = errString(err);
        return v;
    }
    std::vector<navidrome::PodcastChannel> getPodcastChannels(std::string& e) override {
        NSError *err = nil;
        auto v = mapArr<SubsonicPodcastChannel>([client getPodcastChannelsWithError:&err]);
        e = errString(err);
        return v;
    }
    std::vector<navidrome::PodcastEpisode> getPodcastEpisodes(const std::string& channelId,
                                                               std::string& e) override {
        NSError *err = nil;
        auto v = mapArr<SubsonicPodcastEpisode>(
            [client getPodcastEpisodesForChannel:@(channelId.c_str()) error:&err]);
        e = errString(err);
        return v;
    }
    std::vector<navidrome::NowPlayingEntry> getNowPlaying(std::string& e) override {
        NSError *err = nil;
        auto v = mapArr<SubsonicNowPlayingEntry>([client getNowPlayingWithError:&err]);
        e = errString(err);
        return v;
    }
    std::vector<navidrome::Song> getSimilarSongs(const std::string& id, int count,
                                                 std::string& e) override {
        NSError *err = nil;
        auto v = mapArr<SubsonicSong>([client getSimilarSongsForId:@(id.c_str())
                                                               count:count
                                                               error:&err]);
        e = errString(err);
        return v;
    }
    std::vector<navidrome::Song> getRandomSongs(int count, std::string& e) override {
        NSError *err = nil;
        auto v = mapArr<SubsonicSong>([client getRandomSongsWithCount:count error:&err]);
        e = errString(err);
        return v;
    }
    std::vector<navidrome::Song> getAllSongs(std::string& e) override {
        NSError *err = nil;
        auto v = mapArr<SubsonicSong>([client getAllSongsWithError:&err]);
        e = errString(err);
        return v;
    }
    navidrome::ArtistInfo getArtistInfo(const std::string& id, std::string& e) override {
        NSError *err = nil;
        SubsonicArtistInfo *info = [client getArtistInfoForId:@(id.c_str()) error:&err];
        e = errString(err);
        return info ? conv(info) : navidrome::ArtistInfo{};
    }
    std::vector<navidrome::Song> getTopSongs(const std::string& artistName, int count,
                                             std::string& e) override {
        NSError *err = nil;
        auto v = mapArr<SubsonicSong>([client getTopSongsForArtist:@(artistName.c_str())
                                                               count:count
                                                               error:&err]);
        e = errString(err);
        return v;
    }
    navidrome::Lyrics getLyrics(const std::string& id, const std::string& artist,
                                const std::string& title, std::string& e) override {
        NSError *err = nil;
        auto l = [client getLyricsForSongId:@(id.c_str())
                                     artist:@(artist.c_str())
                                      title:@(title.c_str())
                                      error:&err];
        e = errString(err);
        return l;
    }
    std::vector<std::string> groupingLibraryIds() override {
        std::vector<std::string> out;
        for (NSString *x in [client libraryGroupingIds]) out.push_back(str(x));
        return out;
    }
    std::vector<navidrome::MusicFolder> musicFolders() override {
        return mapArr<SubsonicMusicFolder>([client cachedMusicFolders]);
    }
    navidrome::CategoryKindList hiddenCategories() override {
        return navidrome::parseHiddenCategories(navidrome::cfg_browser_hidden_categories.get().c_str());
    }
    bool setStarred(bool starred, const std::string& id, navidrome::StarKind kind,
                    std::string& e) override {
        NSError *err = nil;
        BOOL ok = [client setStarred:starred
                                forId:@(id.c_str())
                                 kind:static_cast<SubsonicStarKind>(kind)
                                error:&err];
        e = errString(err);
        return ok;
    }
    bool setRating(int stars, const std::string& id, std::string& e) override {
        NSError *err = nil;
        BOOL ok = [client setRating:stars forSongId:@(id.c_str()) error:&err];
        e = errString(err);
        return ok;
    }
    bool getSong(const std::string& id, navidrome::Song& out, std::string& e) override {
        NSError *err = nil;
        SubsonicSong *s = [client getSongWithId:@(id.c_str()) error:&err];
        e = errString(err);
        if (!s) { if (e.empty()) e = "song not found"; return false; }
        out = conv(s);
        return true;
    }
};
}

std::unique_ptr<navidrome::IBrowserClient> navidrome::makeMacBrowserClient() {
    return std::make_unique<MacBrowserClient>();
}

bool navidrome::libraryIsConfigured() { return [SubsonicClient.sharedClient isConfigured]; }

navidrome::IBrowserClient& navidrome::libraryClient() {
    static MacBrowserClient inst;
    return inst;
}

bool navidrome::libraryServerInfo(ServerInfo& out, std::string& outError) {
    return [SubsonicClient.sharedClient serverInfo:out error:outError];
}

std::vector<uint8_t> navidrome::libraryFetchCover(const std::string& id, int size, abort_callback& abort) {
    abort.check();
    @autoreleasepool {
        NSURL *url = [SubsonicClient.sharedClient coverArtURLForId:@(id.c_str()) size:size];
        if (!url) return {};
        NSError *err = nil;
        NSData *data = [SubsonicClient.sharedClient dataForURL:url error:&err];
        abort.check();
        if (!data || data.length == 0) {
            NAVIDROME_WARN("Library", "no cover for id=" + id + (err ? " (" + errString(err) + ")" : std::string()));
            return {};
        }
        const auto* p = static_cast<const uint8_t*>(data.bytes);
        return std::vector<uint8_t>(p, p + data.length);
    }
}
