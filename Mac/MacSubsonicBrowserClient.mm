#import "MacSubsonicBrowserClient.h"
#import "../SubsonicClient.h"
#import <Foundation/Foundation.h>
#include <string>
#include <utility>
#include <vector>

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

// Map an ObjC array to std::vector<navidrome::X> via the matching conv()
// overload. The element type is given explicitly, so nothing is deduced from
// the (generics-erased) NSArray type.
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
    std::vector<std::string> groupingLibraryIds() override {
        std::vector<std::string> out;
        for (NSString *x in [client libraryGroupingIds]) out.push_back(str(x));
        return out;
    }
    std::vector<navidrome::MusicFolder> musicFolders() override {
        return mapArr<SubsonicMusicFolder>([client cachedMusicFolders]);
    }
};

} // namespace

std::unique_ptr<navidrome::IBrowserClient> navidrome::makeMacBrowserClient() {
    return std::make_unique<MacBrowserClient>();
}
