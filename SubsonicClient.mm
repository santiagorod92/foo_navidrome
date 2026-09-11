#import "SubsonicClient.h"
#import "SubsonicTypes.h"
#import "SubsonicCore.h"
#import "NavidromeDebugLog.h"

#import <memory>

// Forward declaration of config vars (defined in NavidromePlugin.mm)
namespace navidrome {
    extern cfg_string cfg_server_url;
    extern cfg_string cfg_username;
    extern cfg_string cfg_password;
    extern cfg_string cfg_salt;  // Fixed salt generated once at component load
    extern cfg_string cfg_custom_headers;
    extern cfg_string cfg_stream_format;
    extern cfg_var_modern::cfg_int cfg_max_bitrate;
    extern cfg_var_modern::cfg_bool cfg_library_filter;
    extern cfg_string cfg_library_ids;
}

// ---------------------------------------------------------------------------
// This file is now the macOS adapter over the shared navidrome::SubsonicCore
// (SubsonicCore.h) — the core owns every request body (URL assembly, retry
// loop, status-wrapper check, json walk, multi-library fan-out). Here we
// supply an NSURLSession-backed IHttpTransport + a cfg_*-backed
// ISettingsProvider, keep the bare cover-art / download paths that never went
// through JSON, and marshal the core's std::vector<navidrome::X> back into the
// ObjC Subsonic* view-model the Mac UI consumes.
// ---------------------------------------------------------------------------

// Apply the user-configured custom headers (one "Name: Value" per line) to a
// mutable request — shared by API calls and cover-art fetches so every request
// carries e.g. Cloudflare Access service tokens.
static void NavidromeApplyCustomHeaders(NSMutableURLRequest *req) {
    for (const std::string &line :
         navidrome::parseHeaderLines(navidrome::cfg_custom_headers.get().c_str())) {
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string name = line.substr(0, colon);
        size_t ne = name.find_last_not_of(" \t");
        name = (ne == std::string::npos) ? "" : name.substr(0, ne + 1);
        size_t vs = line.find_first_not_of(" \t", colon + 1);
        std::string value = (vs == std::string::npos) ? "" : line.substr(vs);
        if (name.empty()) continue;
        [req setValue:[NSString stringWithUTF8String:value.c_str()]
            forHTTPHeaderField:[NSString stringWithUTF8String:name.c_str()]];
    }
}

// Map an NSURLErrorDomain code to the shared ErrorKind so the core's retry loop
// can tell a transient socket failure from a dead-certain one.
static navidrome::ErrorKind NavidromeClassifyURLError(NSInteger code) {
    switch (code) {
        case NSURLErrorTimedOut:
            return navidrome::ErrorKind::Timeout;
        case NSURLErrorCancelled:
            return navidrome::ErrorKind::Cancelled;
        case NSURLErrorCannotFindHost:
        case NSURLErrorCannotConnectToHost:
        case NSURLErrorNetworkConnectionLost:
        case NSURLErrorNotConnectedToInternet:
        case NSURLErrorDNSLookupFailed:
        case NSURLErrorResourceUnavailable:
            return navidrome::ErrorKind::Network;
        case NSURLErrorSecureConnectionFailed:
        case NSURLErrorServerCertificateHasBadDate:
        case NSURLErrorServerCertificateUntrusted:
        case NSURLErrorServerCertificateHasUnknownRoot:
        case NSURLErrorServerCertificateNotYetValid:
        case NSURLErrorClientCertificateRejected:
        case NSURLErrorClientCertificateRequired:
            return navidrome::ErrorKind::Tls;
        default:
            return navidrome::ErrorKind::Network;
    }
}

// The server rejecting the configured credentials is a deterministic, user-
// actionable state — say so once per session in the console (every subsequent
// call would just repeat it).
static void NavidromeWarnAuthOnce() {
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        console::print("Navidrome: the server rejected the configured credentials — "
                       "check Preferences › Tools › Navidrome");
    });
}

static NSString *nsstr(const std::string &s) {
    return [NSString stringWithUTF8String:s.c_str()] ?: @"";
}

// ---------------------------------------------------------------------------
// Data model implementations
// ---------------------------------------------------------------------------

@implementation SubsonicArtist
- (NSString *)description {
    return [NSString stringWithFormat:@"<SubsonicArtist %@ %@>", _artistId, _name];
}
@end

@implementation SubsonicAlbum
- (NSString *)description {
    return [NSString stringWithFormat:@"<SubsonicAlbum %@ %@>", _albumId, _name];
}
@end

@implementation SubsonicSong
- (NSString *)description {
    return [NSString stringWithFormat:@"<SubsonicSong %@ %@>", _songId, _title];
}
@end

@implementation SubsonicPlaylist
- (NSString *)description {
    return [NSString stringWithFormat:@"<SubsonicPlaylist %@ %@>", _playlistId, _name];
}
@end

@implementation SubsonicGenre
- (NSString *)description {
    return [NSString stringWithFormat:@"<SubsonicGenre %@>", _name];
}
@end

@implementation SubsonicBookmark
- (NSString *)description {
    return [NSString stringWithFormat:@"<SubsonicBookmark %@ %.0fms>", _song.songId, _positionMs];
}
@end

@implementation SubsonicRadioStation
- (NSString *)description {
    return [NSString stringWithFormat:@"<SubsonicRadioStation %@ %@>", _stationId, _name];
}
@end

@implementation SubsonicMusicFolder
- (NSString *)description {
    return [NSString stringWithFormat:@"<SubsonicMusicFolder %@ %@>", _folderId, _name];
}
@end

// ---------------------------------------------------------------------------
// navidrome::X struct -> ObjC Subsonic* view-model. A straight field copy: the
// json parsing, field names and Subsonic quirks all live in SubsonicTypes.h,
// byte-for-byte shared with the Windows client.
// ---------------------------------------------------------------------------
static SubsonicSong *SongFromCore(const navidrome::Song &s) {
    SubsonicSong *song = [[SubsonicSong alloc] init];
    song.songId     = nsstr(s.id);
    song.title      = nsstr(s.title);
    song.artist     = nsstr(s.artist);
    song.artistId   = nsstr(s.artistId);
    song.album      = nsstr(s.album);
    song.albumId    = nsstr(s.albumId);
    song.track      = s.track;
    song.year       = s.year;
    song.duration   = s.duration;
    song.coverArtId = nsstr(s.coverArtId);
    song.suffix     = nsstr(s.suffix);
    song.starred    = s.starred;
    song.rating     = s.rating;
    return song;
}

static SubsonicAlbum *AlbumFromCore(const navidrome::Album &a) {
    SubsonicAlbum *album = [[SubsonicAlbum alloc] init];
    album.albumId    = nsstr(a.id);
    album.name       = nsstr(a.name);
    album.artist     = nsstr(a.artist);
    album.artistId   = nsstr(a.artistId);
    album.songCount  = a.songCount;
    album.year       = a.year;
    album.coverArtId = nsstr(a.coverArtId);
    album.starred    = a.starred;
    return album;
}

static SubsonicArtist *ArtistFromCore(const navidrome::Artist &a) {
    SubsonicArtist *artist = [[SubsonicArtist alloc] init];
    artist.artistId   = nsstr(a.id);
    artist.name       = nsstr(a.name);
    artist.albumCount = a.albumCount;
    artist.coverArtId = nsstr(a.coverArtId);
    artist.starred    = a.starred;
    return artist;
}

static SubsonicPlaylist *PlaylistFromCore(const navidrome::Playlist &p) {
    SubsonicPlaylist *pl = [[SubsonicPlaylist alloc] init];
    pl.playlistId = nsstr(p.id);
    pl.name       = nsstr(p.name);
    pl.owner      = nsstr(p.owner);
    pl.songCount  = p.songCount;
    pl.duration   = p.duration;
    return pl;
}

static SubsonicGenre *GenreFromCore(const navidrome::Genre &g) {
    SubsonicGenre *genre = [[SubsonicGenre alloc] init];
    genre.name       = nsstr(g.name);
    genre.songCount  = g.songCount;
    genre.albumCount = g.albumCount;
    return genre;
}

static SubsonicMusicFolder *MusicFolderFromCore(const navidrome::MusicFolder &f) {
    SubsonicMusicFolder *mf = [[SubsonicMusicFolder alloc] init];
    mf.folderId = nsstr(f.id);
    mf.name     = nsstr(f.name);
    return mf;
}

static SubsonicRadioStation *RadioStationFromCore(const navidrome::RadioStation &r) {
    SubsonicRadioStation *st = [[SubsonicRadioStation alloc] init];
    st.stationId   = nsstr(r.id);
    st.name        = nsstr(r.name);
    st.streamUrl   = nsstr(r.streamUrl);
    st.homePageUrl = nsstr(r.homePageUrl);
    return st;
}

static SubsonicBookmark *BookmarkFromCore(const navidrome::Bookmark &b) {
    SubsonicBookmark *bm = [[SubsonicBookmark alloc] init];
    bm.song       = SongFromCore(b.song);
    bm.positionMs = b.positionMs;
    bm.comment    = nsstr(b.comment);
    return bm;
}

static NSArray<SubsonicSong *> *SongsFromCore(const std::vector<navidrome::Song> &v) {
    NSMutableArray<SubsonicSong *> *a = [NSMutableArray arrayWithCapacity:v.size()];
    for (const auto &s : v) [a addObject:SongFromCore(s)];
    return a;
}
static NSArray<SubsonicAlbum *> *AlbumsFromCore(const std::vector<navidrome::Album> &v) {
    NSMutableArray<SubsonicAlbum *> *a = [NSMutableArray arrayWithCapacity:v.size()];
    for (const auto &x : v) [a addObject:AlbumFromCore(x)];
    return a;
}
static NSArray<SubsonicArtist *> *ArtistsFromCore(const std::vector<navidrome::Artist> &v) {
    NSMutableArray<SubsonicArtist *> *a = [NSMutableArray arrayWithCapacity:v.size()];
    for (const auto &x : v) [a addObject:ArtistFromCore(x)];
    return a;
}
static NSArray<SubsonicMusicFolder *> *MusicFoldersFromCore(const std::vector<navidrome::MusicFolder> &v) {
    NSMutableArray<SubsonicMusicFolder *> *a = [NSMutableArray arrayWithCapacity:v.size()];
    for (const auto &x : v) {
        SubsonicMusicFolder *mf = MusicFolderFromCore(x);
        if (mf.folderId.length > 0) [a addObject:mf];
    }
    return a;
}
static NSArray<NSString *> *StringsToNSArray(const std::vector<std::string> &v) {
    NSMutableArray<NSString *> *a = [NSMutableArray arrayWithCapacity:v.size()];
    for (const auto &s : v) [a addObject:nsstr(s)];
    return a;
}

// A core method reports failure through a non-empty outError (its "isNull"
// equivalent); turn that into the NSError the ObjC callers expect.
static NSError *NavidromeMakeError(const std::string &msg, NSInteger code) {
    return [NSError errorWithDomain:@"SubsonicClient" code:code userInfo:@{
        NSLocalizedDescriptionKey: (msg.empty() ? @"request failed" : nsstr(msg)) }];
}

static std::vector<std::string> ToStdStrings(NSArray<NSString *> *arr) {
    std::vector<std::string> out;
    out.reserve(arr.count);
    for (NSString *s in arr) out.push_back(s.UTF8String ?: "");
    return out;
}

// ---------------------------------------------------------------------------
// The IHttpTransport + ISettingsProvider SubsonicCore runs on.
// ---------------------------------------------------------------------------
namespace {

// One synchronous NSURLSession GET — no retry (the core drives that), no status
// wrapper parsing. Custom headers applied from the live cfg globals.
struct MacHttpTransport : navidrome::IHttpTransport {
    NSURLSession *session = nil;

    navidrome::HttpResult getOnce(const std::string &url) override {
        navidrome::HttpResult out;
        NSURL *nsurl = [NSURL URLWithString:nsstr(url)];
        if (!nsurl) {
            out.error = { navidrome::ErrorKind::Parse, 0, 0, "invalid URL" };
            return out;
        }
        NSMutableURLRequest *request = [NSMutableURLRequest requestWithURL:nsurl];
        NavidromeApplyCustomHeaders(request);

        __block NSData *data = nil;
        __block NSError *taskError = nil;
        __block NSHTTPURLResponse *httpResponse = nil;
        dispatch_semaphore_t sema = dispatch_semaphore_create(0);
        [[session dataTaskWithRequest:request
                    completionHandler:^(NSData *d, NSURLResponse *response, NSError *error) {
            data = d;
            taskError = error;
            httpResponse = (NSHTTPURLResponse *)response;
            dispatch_semaphore_signal(sema);
        }] resume];
        dispatch_semaphore_wait(sema, DISPATCH_TIME_FOREVER);

        if (taskError) {
            navidrome::ErrorKind kind = NavidromeClassifyURLError(taskError.code);
            out.error = { kind, 0, 0, std::string(navidrome::errorKindName(kind)) + ": " +
                          (taskError.localizedDescription.UTF8String ?: "?") };
            return out;
        }
        int status = (int)httpResponse.statusCode;
        navidrome::ErrorKind kind = navidrome::httpStatusToErrorKind(status);
        if (kind != navidrome::ErrorKind::None) {
            out.error = { kind, status, 0, "HTTP " + std::to_string(status) };
            return out;
        }
        if (!data) {
            out.error = { navidrome::ErrorKind::Parse, status, 0, "empty response body" };
            return out;
        }
        out.body.assign(static_cast<const char *>(data.bytes), data.length);
        out.error = { navidrome::ErrorKind::None, status, 0, {} };
        return out;
    }

    void onAuthRejected() override { NavidromeWarnAuthOnce(); }
};

struct MacSettingsProvider : navidrome::ISettingsProvider {
    navidrome::SubsonicSettings load() const override {
        navidrome::SubsonicSettings s;
        s.serverUrl     = navidrome::cfg_server_url.get().c_str();
        s.username      = navidrome::cfg_username.get().c_str();
        s.password      = navidrome::cfg_password.get().c_str();
        pfc::string8 salt = navidrome::cfg_salt.get();
        s.salt          = salt.length() > 0 ? salt.c_str() : "fb2k_navidrome";
        s.streamFormat  = navidrome::cfg_stream_format.get().c_str();
        s.maxBitrate    = static_cast<int>(navidrome::cfg_max_bitrate.get());
        s.libraryFilter = navidrome::cfg_library_filter.get();
        s.libraryIdsCsv = navidrome::cfg_library_ids.get().c_str();
        return s;
    }
};

}  // namespace

// ---------------------------------------------------------------------------
// SubsonicClient
// ---------------------------------------------------------------------------

@interface SubsonicClient () {
    // Order matters: the transport + settings provider must outlive the core.
    std::unique_ptr<navidrome::IHttpTransport>    _transport;
    std::unique_ptr<navidrome::ISettingsProvider> _settingsProvider;
    std::unique_ptr<navidrome::SubsonicCore>      _core;
}
@property (nonatomic, strong) NSURLSession *session;
@end

@implementation SubsonicClient

+ (instancetype)sharedClient {
    static SubsonicClient *instance = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        instance = [[SubsonicClient alloc] init];
    });
    return instance;
}

- (instancetype)init {
    self = [super init];
    if (self) {
        NSURLSessionConfiguration *config = [NSURLSessionConfiguration defaultSessionConfiguration];
        config.timeoutIntervalForRequest = 15.0;
        config.timeoutIntervalForResource = 30.0;
        _session = [NSURLSession sessionWithConfiguration:config];

        auto transport = std::make_unique<MacHttpTransport>();
        transport->session = _session;
        _transport = std::move(transport);
        _settingsProvider = std::make_unique<MacSettingsProvider>();
        _core = std::make_unique<navidrome::SubsonicCore>(*_transport, *_settingsProvider);
    }
    return self;
}

- (BOOL)isConfigured {
    return _core->isConfigured();
}

- (navidrome::Error)lastError {
    return _core->lastError();
}

// ---------------------------------------------------------------------------
// API Methods — every call forwards to the shared core, then marshals the
// result back into the ObjC view-model.
// ---------------------------------------------------------------------------

- (BOOL)pingWithError:(NSError **)error {
    std::string err;
    BOOL ok = _core->ping(err);
    if (!ok && error) *error = NavidromeMakeError(err, -2);
    return ok;
}

// ---------------------------------------------------------------------------
// Music folders / multi-library filter
// ---------------------------------------------------------------------------

- (NSArray<SubsonicMusicFolder *> *)getMusicFoldersWithError:(NSError **)error {
    std::string err;
    auto folders = _core->getMusicFolders(err);
    if (!err.empty()) { if (error) *error = NavidromeMakeError(err, -2); return nil; }
    return MusicFoldersFromCore(folders);
}

- (void)refreshMusicFolders {
    _core->refreshMusicFolders();
}

- (NSArray<SubsonicMusicFolder *> *)cachedMusicFolders {
    return MusicFoldersFromCore(_core->cachedMusicFolders());
}

- (NSArray<NSString *> *)activeMusicFolderIds {
    return StringsToNSArray(_core->activeMusicFolderIds());
}

- (NSArray<NSString *> *)libraryGroupingIds {
    return StringsToNSArray(_core->libraryGroupingIds());
}

// ---------------------------------------------------------------------------
// Browse
// ---------------------------------------------------------------------------

- (NSArray<SubsonicArtist *> *)getArtistsWithError:(NSError **)error {
    std::string err;
    auto v = _core->getArtists(err);
    if (!err.empty()) { if (error) *error = NavidromeMakeError(err, -2); return nil; }
    return ArtistsFromCore(v);
}

- (NSArray<SubsonicArtist *> *)getArtistsForLibrary:(NSString *)libraryId
                                              error:(NSError **)error {
    std::string err;
    auto v = _core->getArtistsForLibrary(libraryId.UTF8String ?: "", err);
    if (!err.empty()) { if (error) *error = NavidromeMakeError(err, -2); return @[]; }
    return ArtistsFromCore(v);
}

- (NSArray<SubsonicAlbum *> *)getAlbumsForArtist:(NSString *)artistId error:(NSError **)error {
    return [self getAlbumsForArtist:artistId error:error scopeLibrary:nil];
}

- (NSArray<SubsonicAlbum *> *)getAlbumsForArtist:(NSString *)artistId
                                            error:(NSError **)error
                                   scopeLibrary:(NSString *)scopeLibraryId {
    std::string err;
    auto v = _core->getAlbumsForArtist(artistId.UTF8String ?: "", err,
                                       scopeLibraryId.UTF8String ?: "");
    if (!err.empty()) { if (error) *error = NavidromeMakeError(err, -2); return nil; }
    return AlbumsFromCore(v);
}

- (NSArray<SubsonicSong *> *)getSongsForAlbum:(NSString *)albumId error:(NSError **)error {
    std::string err;
    auto v = _core->getSongsForAlbum(albumId.UTF8String ?: "", err);
    if (!err.empty()) { if (error) *error = NavidromeMakeError(err, -2); return nil; }
    return SongsFromCore(v);
}

- (NSDictionary *)search:(NSString *)query error:(NSError **)error {
    std::string err;
    navidrome::SearchResults r = _core->search(query.UTF8String ?: "", err);
    if (!err.empty() && r.artists.empty() && r.albums.empty() && r.songs.empty()) {
        if (error) *error = NavidromeMakeError(err, -2);
        return nil;
    }
    return @{ @"artists": ArtistsFromCore(r.artists),
              @"albums":  AlbumsFromCore(r.albums),
              @"songs":   SongsFromCore(r.songs) };
}

// ---------------------------------------------------------------------------
// Smart lists, favorites, ratings, playlists, scrobbling
// ---------------------------------------------------------------------------

- (NSArray<SubsonicAlbum *> *)getAlbumListOfType:(NSString *)type
                                            size:(NSInteger)size
                                           error:(NSError **)error {
    navidrome::AlbumListType t = navidrome::AlbumListType::Newest;
    NSString *lc = type.lowercaseString;
    if ([lc isEqualToString:@"frequent"]) t = navidrome::AlbumListType::Frequent;
    else if ([lc isEqualToString:@"recent"])  t = navidrome::AlbumListType::Recent;
    else if ([lc isEqualToString:@"random"])  t = navidrome::AlbumListType::Random;
    else if ([lc isEqualToString:@"starred"]) t = navidrome::AlbumListType::Starred;

    std::string err;
    auto v = _core->getAlbumList(t, (int)size, err);
    if (!err.empty()) { if (error) *error = NavidromeMakeError(err, -2); return nil; }
    return AlbumsFromCore(v);
}

- (NSArray<SubsonicSong *> *)getStarredSongsWithError:(NSError **)error {
    std::string err;
    auto v = _core->getStarredSongs(err);
    if (!err.empty()) { if (error) *error = NavidromeMakeError(err, -2); return nil; }
    return SongsFromCore(v);
}

- (NSArray<SubsonicGenre *> *)getGenresWithError:(NSError **)error {
    std::string err;
    auto v = _core->getGenres(err);
    if (!err.empty()) { if (error) *error = NavidromeMakeError(err, -2); return nil; }
    NSMutableArray<SubsonicGenre *> *result = [NSMutableArray arrayWithCapacity:v.size()];
    for (const auto &g : v) [result addObject:GenreFromCore(g)];
    return result;
}

- (NSArray<SubsonicSong *> *)getSongsForGenre:(NSString *)genre
                                        count:(NSInteger)count
                                        error:(NSError **)error {
    if (genre.length == 0) return @[];
    std::string err;
    auto v = _core->getSongsForGenre(genre.UTF8String ?: "", (int)count, err);
    if (!err.empty()) { if (error) *error = NavidromeMakeError(err, -2); return nil; }
    return SongsFromCore(v);
}

- (NSArray<SubsonicSong *> *)getSimilarSongsForId:(NSString *)itemId
                                             count:(NSInteger)count
                                             error:(NSError **)error {
    if (itemId.length == 0) return @[];
    std::string err;
    auto v = _core->getSimilarSongs(itemId.UTF8String ?: "", (int)count, err);
    if (!err.empty()) { if (error) *error = NavidromeMakeError(err, -2); return nil; }
    return SongsFromCore(v);
}

- (NSArray<SubsonicSong *> *)getRandomSongsWithCount:(NSInteger)count
                                                error:(NSError **)error {
    std::string err;
    auto v = _core->getRandomSongs((int)count, err);
    if (!err.empty()) { if (error) *error = NavidromeMakeError(err, -2); return nil; }
    return SongsFromCore(v);
}

- (BOOL)setStarred:(BOOL)starred
             forId:(NSString *)itemId
              kind:(SubsonicStarKind)kind
             error:(NSError **)error {
    navidrome::StarKind k = navidrome::StarKind::Song;
    if (kind == SubsonicStarKindAlbum)  k = navidrome::StarKind::Album;
    if (kind == SubsonicStarKindArtist) k = navidrome::StarKind::Artist;

    std::string err;
    BOOL ok = _core->setStarred(starred, itemId.UTF8String ?: "", k, err);
    if (!ok && error) *error = NavidromeMakeError(err, -2);
    return ok;
}

- (SubsonicSong *)getSongWithId:(NSString *)songId error:(NSError **)error {
    if (songId.length == 0) return nil;
    std::string err;
    navidrome::Song s;
    if (!_core->getSong(songId.UTF8String ?: "", s, err)) {
        if (error) *error = NavidromeMakeError(err, -2);
        return nil;
    }
    return SongFromCore(s);
}

- (BOOL)setRating:(NSInteger)rating forSongId:(NSString *)songId error:(NSError **)error {
    std::string err;
    BOOL ok = _core->setRating((int)rating, songId.UTF8String ?: "", err);
    if (!ok && error) *error = NavidromeMakeError(err, -2);
    return ok;
}

- (NSArray<SubsonicPlaylist *> *)getPlaylistsWithError:(NSError **)error {
    std::string err;
    auto v = _core->getPlaylists(err);
    if (!err.empty()) { if (error) *error = NavidromeMakeError(err, -2); return nil; }
    NSMutableArray<SubsonicPlaylist *> *result = [NSMutableArray arrayWithCapacity:v.size()];
    for (const auto &p : v) [result addObject:PlaylistFromCore(p)];
    return result;
}

- (NSArray<SubsonicSong *> *)getPlaylistSongs:(NSString *)playlistId error:(NSError **)error {
    std::string err;
    auto v = _core->getPlaylistSongs(playlistId.UTF8String ?: "", err);
    if (!err.empty()) { if (error) *error = NavidromeMakeError(err, -2); return nil; }
    return SongsFromCore(v);
}

- (NSString *)createPlaylistNamed:(NSString *)name
                          songIds:(NSArray<NSString *> *)songIds
                            error:(NSError **)error {
    if (name.length == 0) return nil;
    std::string err;
    std::string pid = _core->createPlaylist(name.UTF8String ?: "", ToStdStrings(songIds), err);
    if (!err.empty()) { if (error) *error = NavidromeMakeError(err, -3); return nil; }
    // "" means "created, server echoed no id" — a success, per the header contract.
    return nsstr(pid);
}

- (BOOL)addSongs:(NSArray<NSString *> *)songIds
      toPlaylist:(NSString *)playlistId
           error:(NSError **)error {
    std::string err;
    BOOL ok = _core->addToPlaylist(playlistId.UTF8String ?: "", ToStdStrings(songIds), err);
    if (!ok && error) *error = NavidromeMakeError(err, -2);
    return ok;
}

- (BOOL)removeIndexes:(NSArray<NSNumber *> *)indexes
         fromPlaylist:(NSString *)playlistId
                error:(NSError **)error {
    std::vector<int> idx;
    idx.reserve(indexes.count);
    for (NSNumber *n in indexes) idx.push_back(n.intValue);
    std::string err;
    BOOL ok = _core->removeFromPlaylist(playlistId.UTF8String ?: "", idx, err);
    if (!ok && error) *error = NavidromeMakeError(err, -2);
    return ok;
}

- (BOOL)renamePlaylist:(NSString *)playlistId
                toName:(NSString *)name
                 error:(NSError **)error {
    std::string err;
    BOOL ok = _core->renamePlaylist(playlistId.UTF8String ?: "", name.UTF8String ?: "", err);
    if (!ok && error) *error = NavidromeMakeError(err, -2);
    return ok;
}

- (BOOL)deletePlaylist:(NSString *)playlistId error:(NSError **)error {
    std::string err;
    BOOL ok = _core->deletePlaylist(playlistId.UTF8String ?: "", err);
    if (!ok && error) *error = NavidromeMakeError(err, -2);
    return ok;
}

- (NSArray<SubsonicRadioStation *> *)getRadioStationsWithError:(NSError **)error {
    std::string err;
    auto v = _core->getRadioStations(err);
    if (!err.empty()) { if (error) *error = NavidromeMakeError(err, -2); return nil; }
    NSMutableArray<SubsonicRadioStation *> *result = [NSMutableArray arrayWithCapacity:v.size()];
    for (const auto &r : v) [result addObject:RadioStationFromCore(r)];
    return result;
}

- (NSString *)createRadioStationWithStreamURL:(NSString *)streamUrl
                                          name:(NSString *)name
                                   homePageUrl:(NSString *)homePageUrl
                                         error:(NSError **)error {
    if (streamUrl.length == 0 || name.length == 0) return nil;
    std::string err;
    _core->createRadioStation(streamUrl.UTF8String ?: "", name.UTF8String ?: "",
                              homePageUrl.UTF8String ?: "", err);
    if (!err.empty()) { if (error) *error = NavidromeMakeError(err, -2); return nil; }
    // Subsonic's create-station endpoint echoes no id back — success is @"".
    return @"";
}

- (BOOL)updateRadioStation:(NSString *)stationId
                  streamURL:(NSString *)streamUrl
                       name:(NSString *)name
                homePageUrl:(NSString *)homePageUrl
                      error:(NSError **)error {
    std::string err;
    BOOL ok = _core->updateRadioStation(stationId.UTF8String ?: "", streamUrl.UTF8String ?: "",
                                        name.UTF8String ?: "", homePageUrl.UTF8String ?: "", err);
    if (!ok && error) *error = NavidromeMakeError(err, -2);
    return ok;
}

- (BOOL)deleteRadioStation:(NSString *)stationId error:(NSError **)error {
    std::string err;
    BOOL ok = _core->deleteRadioStation(stationId.UTF8String ?: "", err);
    if (!ok && error) *error = NavidromeMakeError(err, -2);
    return ok;
}

- (NSArray<SubsonicBookmark *> *)getBookmarksWithError:(NSError **)error {
    std::string err;
    auto v = _core->getBookmarks(err);
    if (!err.empty()) { if (error) *error = NavidromeMakeError(err, -2); return nil; }
    NSMutableArray<SubsonicBookmark *> *result = [NSMutableArray arrayWithCapacity:v.size()];
    for (const auto &b : v) [result addObject:BookmarkFromCore(b)];
    return result;
}

- (BOOL)createBookmarkForSongId:(NSString *)songId
                      positionMs:(NSTimeInterval)positionMs
                         comment:(NSString *)comment
                           error:(NSError **)error {
    std::string err;
    BOOL ok = _core->createBookmark(songId.UTF8String ?: "", positionMs,
                                    comment.UTF8String ?: "", err);
    if (!ok && error) *error = NavidromeMakeError(err, -2);
    return ok;
}

- (BOOL)deleteBookmarkForSongId:(NSString *)songId error:(NSError **)error {
    std::string err;
    BOOL ok = _core->deleteBookmark(songId.UTF8String ?: "", err);
    if (!ok && error) *error = NavidromeMakeError(err, -2);
    return ok;
}

- (BOOL)startScanWithScanning:(BOOL *)scanning count:(NSInteger *)count error:(NSError **)error {
    if (scanning) *scanning = NO;
    if (count)    *count    = 0;
    std::string err;
    navidrome::ScanStatus st = _core->startScan(err);
    if (!err.empty()) { if (error) *error = NavidromeMakeError(err, -2); return NO; }
    if (scanning) *scanning = st.scanning;
    if (count)    *count    = static_cast<NSInteger>(st.count);
    return YES;
}

- (BOOL)getScanStatusWithScanning:(BOOL *)scanning count:(NSInteger *)count error:(NSError **)error {
    if (scanning) *scanning = NO;
    if (count)    *count    = 0;
    std::string err;
    navidrome::ScanStatus st = _core->getScanStatus(err);
    if (!err.empty()) { if (error) *error = NavidromeMakeError(err, -2); return NO; }
    if (scanning) *scanning = st.scanning;
    if (count)    *count    = static_cast<NSInteger>(st.count);
    return YES;
}

- (BOOL)scrobbleSongId:(NSString *)songId
            submission:(BOOL)submission
                 error:(NSError **)error {
    std::string err;
    BOOL ok = _core->scrobble(songId.UTF8String ?: "", submission, err);
    if (!ok && error) *error = NavidromeMakeError(err, -2);
    return ok;
}

// ---------------------------------------------------------------------------
// URL builders
// ---------------------------------------------------------------------------

- (NSString *)streamURLForSongId:(NSString *)songId coverArtId:(NSString *)coverArtId {
    return nsstr(_core->streamURL(songId.UTF8String ?: "", coverArtId.UTF8String ?: ""));
}

- (NSURL *)downloadURLForSongId:(NSString *)songId {
    return [NSURL URLWithString:nsstr(_core->downloadURL(songId.UTF8String ?: ""))];
}

- (NSURL *)coverArtURLForId:(NSString *)coverArtId size:(NSInteger)size {
    return [NSURL URLWithString:nsstr(_core->coverArtURL(coverArtId.UTF8String ?: "", (int)size))];
}

// ---------------------------------------------------------------------------
// Bare byte fetch + streaming download — never went through the JSON path, so
// they stay here rather than in the core.
// ---------------------------------------------------------------------------

- (NSData *)dataForURL:(NSURL *)url error:(NSError **)outError {
    const std::string safeUrl =
        navidrome::dbg::scrubAuth(std::string(url.absoluteString.UTF8String ?: ""));
    NAVIDROME_TIMER("Art", "GET " + safeUrl);

    NSMutableURLRequest *request = [NSMutableURLRequest requestWithURL:url];
    NavidromeApplyCustomHeaders(request);

    const int kMaxAttempts = 3;
    for (int attempt = 1; attempt <= kMaxAttempts; ++attempt) {
        __block NSData *responseData = nil;
        __block NSError *taskError = nil;
        __block NSHTTPURLResponse *httpResponse = nil;
        dispatch_semaphore_t sema = dispatch_semaphore_create(0);
        [[_session dataTaskWithRequest:request completionHandler:^(NSData *data, NSURLResponse *response, NSError *error) {
            responseData = data;
            taskError = error;
            httpResponse = (NSHTTPURLResponse *)response;
            dispatch_semaphore_signal(sema);
        }] resume];
        dispatch_semaphore_wait(sema, DISPATCH_TIME_FOREVER);

        navidrome::Error err;
        if (taskError) {
            err.kind = NavidromeClassifyURLError(taskError.code);
            err.message = taskError.localizedDescription.UTF8String ?: "?";
        } else {
            int status = (int)httpResponse.statusCode;
            err.http = status;
            err.kind = navidrome::httpStatusToErrorKind(status);
            if (!err.ok()) err.message = "HTTP " + std::to_string(status);
        }

        if (err.ok()) {
            NAVIDROME_LOG("Art", "200 OK  " + std::to_string((unsigned long)responseData.length) + " bytes");
            return responseData;
        }
        if (!err.retryable() || attempt == kMaxAttempts) {
            NAVIDROME_WARN("Art", std::string(err.kindName()) + ": " + err.message +
                           "  (" + safeUrl + ")");
            if (outError) {
                *outError = taskError ?: [NSError errorWithDomain:@"SubsonicClient"
                    code:err.http userInfo:@{NSLocalizedDescriptionKey:
                        [NSString stringWithUTF8String:err.message.c_str()]}];
            }
            return nil;
        }
        [NSThread sleepForTimeInterval:0.3 * attempt];
    }
    return nil;
}

- (BOOL)downloadURL:(NSURL *)url toPath:(NSString *)path error:(NSError **)outError {
    NSMutableURLRequest *request = [NSMutableURLRequest requestWithURL:url];
    NavidromeApplyCustomHeaders(request);
    // A track download can outlast the 30 s resource timeout the shared session
    // uses for API calls.
    request.timeoutInterval = 300.0;

    __block NSURL *tempURL = nil;
    __block NSError *taskError = nil;
    __block NSHTTPURLResponse *httpResponse = nil;
    dispatch_semaphore_t sema = dispatch_semaphore_create(0);
    [[_session downloadTaskWithRequest:request
                     completionHandler:^(NSURL *location, NSURLResponse *response, NSError *error) {
        taskError    = error;
        httpResponse = (NSHTTPURLResponse *)response;
        // The temp file is deleted as soon as this handler returns, so move it
        // to its final home here rather than after the semaphore is signalled.
        if (location && !error && httpResponse.statusCode == 200) {
            NSError *moveErr = nil;
            [[NSFileManager defaultManager] removeItemAtPath:path error:nil];
            if ([[NSFileManager defaultManager] moveItemAtURL:location
                                                        toURL:[NSURL fileURLWithPath:path]
                                                        error:&moveErr]) {
                tempURL = location;
            } else {
                taskError = moveErr;
            }
        }
        dispatch_semaphore_signal(sema);
    }] resume];
    dispatch_semaphore_wait(sema, DISPATCH_TIME_FOREVER);

    if (taskError) { if (outError) *outError = taskError; return NO; }
    if (httpResponse && httpResponse.statusCode != 200) {
        if (outError) *outError = [NSError errorWithDomain:@"SubsonicClient"
            code:httpResponse.statusCode userInfo:@{NSLocalizedDescriptionKey:
                [NSString stringWithFormat:@"HTTP %ld", (long)httpResponse.statusCode]}];
        return NO;
    }
    return tempURL != nil;
}

@end
