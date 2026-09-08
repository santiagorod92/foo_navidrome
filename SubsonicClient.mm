#import "SubsonicClient.h"
#import "SubsonicTypes.h"
#import "NavidromeDebugLog.h"
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#import <CommonCrypto/CommonDigest.h>
#pragma clang diagnostic pop

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

// Map an NSURLErrorDomain code to the shared ErrorKind so the retry loop can
// tell a transient socket failure from a dead-certain one.
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
// Helpers
// ---------------------------------------------------------------------------

static NSString *md5HexString(NSString *input) {
    const char *cStr = [input UTF8String];
    unsigned char digest[CC_MD5_DIGEST_LENGTH];
    CC_MD5(cStr, (CC_LONG)strlen(cStr), digest);
    NSMutableString *hex = [NSMutableString stringWithCapacity:CC_MD5_DIGEST_LENGTH * 2];
    for (int i = 0; i < CC_MD5_DIGEST_LENGTH; i++) {
        [hex appendFormat:@"%02x", digest[i]];
    }
    return hex;
}

static NSString *urlEncode(NSString *s) {
    return [s stringByAddingPercentEncodingWithAllowedCharacters:
            [NSCharacterSet URLQueryAllowedCharacterSet]];
}

static NSString *nsstr(const std::string &s) {
    return [NSString stringWithUTF8String:s.c_str()] ?: @"";
}

// ObjC wrapper over navidrome::appendMusicFolderParam (SubsonicTypes.h) — the
// `&musicFolderId=<id>` append, shared with the Windows client. A nil/empty
// folderId leaves params untouched.
static NSString *appendMusicFolder(NSString *params, NSString *folderId) {
    std::string out = navidrome::appendMusicFolderParam(
        std::string(params.UTF8String ?: ""), std::string(folderId.UTF8String ?: ""));
    return nsstr(out);
}

// JSON is parsed by navidrome::json (SubsonicTypes.h) and every Subsonic object
// is mapped to a pure-C++ struct there — byte-for-byte shared with the Windows
// client. The ObjC model classes stay as the view-model the Mac UI consumes;
// these shims are the only per-platform code, a straight field copy from the
// shared struct. -fetchInner: returns the inner "subsonic-response" object.
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

// ---------------------------------------------------------------------------
// SubsonicClient
// ---------------------------------------------------------------------------

@interface SubsonicClient () {
    navidrome::Error _lastError;
    NSArray<SubsonicMusicFolder *> *_musicFoldersCache;  // nil until first good fetch
    BOOL _musicFoldersFetched;
}
@property (nonatomic, strong) NSURLSession *session;
// Synchronous GET → the inner "subsonic-response" object (a Null json::Value on
// any failure; check .isNull()). Retries transient failures, classifies the
// outcome into _lastError.
- (navidrome::json::Value)fetchInner:(NSURL *)url error:(NSError **)error;
// startScan.view / getScanStatus.view share this response shape.
- (BOOL)fetchScanStatusForEndpoint:(NSString *)endpoint
                           scanning:(BOOL *)scanning
                              count:(NSInteger *)count
                              error:(NSError **)error;
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
    }
    return self;
}

- (BOOL)isConfigured {
    pfc::string8 url  = navidrome::cfg_server_url.get();
    pfc::string8 user = navidrome::cfg_username.get();
    pfc::string8 pass = navidrome::cfg_password.get();
    return (url.length() > 0 && user.length() > 0 && pass.length() > 0);
}

// Build the common auth query string
- (NSString *)authParams {
    NSString *username = [NSString stringWithUTF8String:navidrome::cfg_username.get().c_str()];
    NSString *password = [NSString stringWithUTF8String:navidrome::cfg_password.get().c_str()];
    pfc::string8 saltPfc = navidrome::cfg_salt.get();
    NSString *salt = saltPfc.length() > 0
        ? [NSString stringWithUTF8String:saltPfc.c_str()]
        : @"navidrome";
    NSString *token    = md5HexString([password stringByAppendingString:salt]);
    return [NSString stringWithFormat:@"u=%@&t=%@&s=%@&v=1.16.1&c=foo_navidrome&f=json",
            urlEncode(username), token, salt];
}

// Build a full API URL for the given endpoint + extra params
- (NSURL *)urlForEndpoint:(NSString *)endpoint params:(NSString *)params {
    NSString *base = [NSString stringWithUTF8String:navidrome::cfg_server_url.get().c_str()];
    // Strip trailing slash
    while ([base hasSuffix:@"/"]) {
        base = [base substringToIndex:base.length - 1];
    }
    NSString *auth = [self authParams];
    NSString *full;
    if (params.length > 0) {
        full = [NSString stringWithFormat:@"%@/rest/%@?%@&%@", base, endpoint, auth, params];
    } else {
        full = [NSString stringWithFormat:@"%@/rest/%@?%@", base, endpoint, auth];
    }
    return [NSURL URLWithString:full];
}

- (navidrome::Error)lastError {
    return _lastError;
}

// Synchronous HTTP GET → the inner "subsonic-response" object, or a Null
// json::Value on failure. Retries transient failures (timeout / 5xx /
// connection reset) up to 3x with backoff; deterministic failures (auth, 404,
// bad JSON) return immediately. Classified outcome is stashed in _lastError.
- (navidrome::json::Value)fetchInner:(NSURL *)url error:(NSError **)outError {
    const std::string safeUrl =
        navidrome::dbg::scrubAuth(std::string(url.absoluteString.UTF8String ?: ""));
    NAVIDROME_TIMER("HTTP", "GET " + safeUrl);
    NAVIDROME_LOG("HTTP", "GET " + safeUrl);
    _lastError = navidrome::Error{};

    NSMutableURLRequest *request = [NSMutableURLRequest requestWithURL:url];
    NavidromeApplyCustomHeaders(request);

    NSData *responseData = nil;

    for (int attempt = 1; attempt <= navidrome::retry::kMaxAttempts; ++attempt) {
        __block NSData *data = nil;
        __block NSError *taskError = nil;
        __block NSHTTPURLResponse *httpResponse = nil;

        dispatch_semaphore_t sema = dispatch_semaphore_create(0);
        [[_session dataTaskWithRequest:request completionHandler:^(NSData *d, NSURLResponse *response, NSError *error) {
            data = d;
            taskError = error;
            httpResponse = (NSHTTPURLResponse *)response;
            dispatch_semaphore_signal(sema);
        }] resume];
        dispatch_semaphore_wait(sema, DISPATCH_TIME_FOREVER);

        navidrome::Error err;
        if (taskError) {
            err.kind    = NavidromeClassifyURLError(taskError.code);
            err.message = std::string(navidrome::errorKindName(err.kind)) + ": " +
                          (taskError.localizedDescription.UTF8String ?: "?");
        } else {
            int status = (int)httpResponse.statusCode;
            err.http = status;
            err.kind = navidrome::httpStatusToErrorKind(status);
            if (!err.ok())
                err.message = "HTTP " + std::to_string(status);
            else if (!data)
                err = { navidrome::ErrorKind::Parse, status, 0, "empty response body" };
        }

        if (err.ok()) {
            responseData = data;
            break;
        }

        _lastError = err;
        if (!navidrome::retry::again(err, attempt)) {
            NAVIDROME_ERR("HTTP", std::string(err.kindName()) + ": " + err.message +
                          "  (" + safeUrl + ")");
            if (err.kind == navidrome::ErrorKind::Auth) NavidromeWarnAuthOnce();
            if (outError) {
                *outError = taskError ?: [NSError errorWithDomain:@"SubsonicClient"
                    code:err.http
                    userInfo:@{NSLocalizedDescriptionKey:
                               [NSString stringWithUTF8String:err.message.c_str()]}];
            }
            return navidrome::json::Value{};
        }

        double backoff = navidrome::retry::backoffMs(attempt, arc4random_uniform(200)) / 1000.0;
        NAVIDROME_WARN("HTTP", err.message + " — retry " + std::to_string(attempt + 1) +
                       "/" + std::to_string(navidrome::retry::kMaxAttempts) + " in " +
                       std::to_string(backoff) + "s  (" + safeUrl + ")");
        [NSThread sleepForTimeInterval:backoff];
    }

    if (!responseData) return navidrome::json::Value{};   // unreachable: loop returns on final failure
    std::string body(static_cast<const char *>(responseData.bytes), responseData.length);
    navidrome::SubsonicResponse resp = navidrome::parseSubsonicResponse(body);
    if (!resp.ok) {
        _lastError = resp.error;
        if (resp.error.code != 0) {
            NAVIDROME_ERR("API", "Subsonic status != ok (code " +
                          std::to_string(resp.error.code) + ", " +
                          _lastError.kindName() + "): " + _lastError.message);
        } else {
            NAVIDROME_ERR("HTTP", std::string(_lastError.kindName()) + ": " + _lastError.message);
        }
        if (resp.error.kind == navidrome::ErrorKind::Auth) NavidromeWarnAuthOnce();
        if (outError) {
            *outError = [NSError errorWithDomain:@"SubsonicClient"
                code:(resp.error.code ? resp.error.code : -2)
                userInfo:@{NSLocalizedDescriptionKey: nsstr(_lastError.message)}];
        }
        return navidrome::json::Value{};
    }

    NAVIDROME_LOG("HTTP", "200 OK  " + std::to_string((unsigned long)responseData.length) + " bytes");
    return resp.inner();
}

// ---------------------------------------------------------------------------
// API Methods
// ---------------------------------------------------------------------------

- (BOOL)pingWithError:(NSError **)error {
    NSURL *url = [self urlForEndpoint:@"ping.view" params:@""];
    return ![self fetchInner:url error:error].isNull();
}

// ---------------------------------------------------------------------------
// Music folders / multi-library filter
// ---------------------------------------------------------------------------

- (NSArray<SubsonicMusicFolder *> *)getMusicFoldersWithError:(NSError **)error {
    NSURL *url = [self urlForEndpoint:@"getMusicFolders.view" params:@""];
    navidrome::json::Value root = [self fetchInner:url error:error];
    if (root.isNull()) return nil;

    NSMutableArray<SubsonicMusicFolder *> *result = [NSMutableArray array];
    for (auto *f : root["musicFolders"]["musicFolder"].items()) {
        SubsonicMusicFolder *mf = MusicFolderFromCore(navidrome::parseMusicFolder(*f));
        if (mf.folderId.length > 0) [result addObject:mf];
    }
    return result;
}

- (void)refreshMusicFolders {
    _musicFoldersCache = nil;
    _musicFoldersFetched = NO;
}

- (NSArray<SubsonicMusicFolder *> *)cachedMusicFolders {
    if (!_musicFoldersFetched) {
        NSArray<SubsonicMusicFolder *> *fetched = [self getMusicFoldersWithError:nil];
        if (fetched.count > 0) {          // latch only on a good answer; retry after a failure
            _musicFoldersCache = fetched;
            _musicFoldersFetched = YES;
        }
    }
    return _musicFoldersCache ?: @[];
}

// The musicFolderId values a browse/search request should fan out over, per the
// cfg_library_filter toggle + cfg_library_ids selection + the cached server
// folder list. Empty array => one request, no musicFolderId (unchanged path).
- (NSArray<NSString *> *)activeMusicFolderIds {
    if (!navidrome::cfg_library_filter.get()) return @[];

    std::vector<navidrome::MusicFolder> folders;
    for (SubsonicMusicFolder *f in [self cachedMusicFolders]) {
        navidrome::MusicFolder mf;
        mf.id   = f.folderId.UTF8String ?: "";
        mf.name = f.name.UTF8String ?: "";
        folders.push_back(std::move(mf));
    }
    auto ids = navidrome::effectiveMusicFolderIds(
        true, navidrome::cfg_library_ids.get().c_str(), folders);

    NSMutableArray<NSString *> *out = [NSMutableArray arrayWithCapacity:ids.size()];
    for (const auto &s : ids)
        [out addObject:[NSString stringWithUTF8String:s.c_str()]];
    return out;
}

// Library ids the browser shows as top-level "group by library" nodes. A 2+
// library server ALWAYS groups (independent of the "Only include selected
// libraries" checkbox); the checkbox only narrows which libraries appear, and
// only when 2+ are ticked. Returns @[] for a single-library server, or when the
// filter is on with exactly one library ticked (single-library scope, shown
// flat via -activeMusicFolderIds). Browser groups when this has 2+ entries.
- (NSArray<NSString *> *)libraryGroupingIds {
    NSArray<SubsonicMusicFolder *> *folders = [self cachedMusicFolders];
    if (folders.count < 2) return @[];

    NSMutableArray<NSString *> *allIds = [NSMutableArray array];
    for (SubsonicMusicFolder *f in folders)
        if (f.folderId) [allIds addObject:f.folderId];

    if (navidrome::cfg_library_filter.get()) {
        auto sel = navidrome::parseMusicFolderIds(navidrome::cfg_library_ids.get().c_str());
        NSMutableArray<NSString *> *picked = [NSMutableArray array];
        for (NSString *lid in allIds) {
            std::string fid = lid.UTF8String ?: "";
            if (std::find(sel.begin(), sel.end(), fid) != sel.end())
                [picked addObject:lid];
        }
        if (picked.count >= 2) return picked;
        if (picked.count == 1) return @[];   // scoped to one library → flat
        // 0 ticked → fall through to "all libraries"
    }
    return allIds;
}

// Run `fetch` once per folder id (or once with nil when the list is empty),
// concatenating results and dropping duplicates by -valueForKey:idKey.
- (NSArray *)fanOutOverFolders:(NSArray<NSString *> *)folderIds
                         idKey:(NSString *)idKey
                         fetch:(NSArray *(^)(NSString *folderId))fetch {
    if (folderIds.count == 0) return fetch(nil);

    NSMutableArray *merged = [NSMutableArray array];
    NSMutableSet<NSString *> *seen = [NSMutableSet set];
    for (NSString *fid in folderIds) {
        for (id item in (fetch(fid) ?: @[])) {
            NSString *iid = [item valueForKey:idKey];
            if (iid.length > 0 && [seen containsObject:iid]) continue;
            if (iid.length > 0) [seen addObject:iid];
            [merged addObject:item];
        }
    }
    return merged;
}

// Parse one getArtists.view response, optionally restricted to a single
// library. folderId nil/empty => no musicFolderId param.
- (NSArray<SubsonicArtist *> *)fetchArtistsForFolder:(NSString *)folderId
                                               error:(NSError **)error {
    NSURL *url = [self urlForEndpoint:@"getArtists.view"
                               params:appendMusicFolder(@"", folderId)];
    navidrome::json::Value root = [self fetchInner:url error:error];
    if (root.isNull()) return nil;

    NSMutableArray<SubsonicArtist *> *result = [NSMutableArray array];
    for (auto *index : root["artists"]["index"].items())
        for (auto *a : (*index)["artist"].items())
            [result addObject:ArtistFromCore(navidrome::parseArtist(*a))];
    return result;
}

- (NSArray<SubsonicArtist *> *)getArtistsWithError:(NSError **)error {
    return [self fanOutOverFolders:[self activeMusicFolderIds]
                             idKey:@"artistId"
                             fetch:^NSArray *(NSString *folderId) {
        return [self fetchArtistsForFolder:folderId error:error];
    }];
}

- (NSArray<SubsonicArtist *> *)getArtistsForLibrary:(NSString *)libraryId
                                              error:(NSError **)error {
    return [self fetchArtistsForFolder:libraryId error:error] ?: @[];
}

- (NSArray<SubsonicAlbum *> *)getAlbumsForArtist:(NSString *)artistId error:(NSError **)error {
    return [self getAlbumsForArtist:artistId error:error scopeLibrary:nil];
}

- (NSArray<SubsonicAlbum *> *)getAlbumsForArtist:(NSString *)artistId
                                            error:(NSError **)error
                                   scopeLibrary:(NSString *)scopeLibraryId {
    NSString *params = [NSString stringWithFormat:@"id=%@", urlEncode(artistId)];
    NSURL *url = [self urlForEndpoint:@"getArtist.view" params:params];
    navidrome::json::Value root = [self fetchInner:url error:error];
    if (root.isNull()) return nil;

    const navidrome::json::Value &artistObj = root["artist"];
    NSString *artistName = nsstr(navidrome::jStr(artistObj, "name"));
    const std::string artistIdStr = artistId.UTF8String ?: "";

    std::vector<navidrome::Album> albums;
    for (auto *a : artistObj["album"].items()) {
        navidrome::Album al = navidrome::parseAlbum(*a);
        if (al.artistId.empty()) al.artistId = artistIdStr;
        albums.push_back(std::move(al));
    }

    // getArtist.view ignores musicFolderId server-side and AlbumID3 carries no
    // library id, so a scoped album list can't come from it directly. search3.view
    // *does* honor musicFolderId: fan it out over the scoped libraries, then
    // navidrome::filterAlbumsByArtistSearch keeps the getArtist list to the ids
    // search confirmed (order preserved). scopeLibraryId pins to one library.
    NSArray<NSString *> *folderIds = scopeLibraryId.length
        ? @[ scopeLibraryId ] : [self activeMusicFolderIds];
    if (folderIds.count > 0 && !albums.empty() && artistName.length > 0) {
        NSString *base = [NSString stringWithFormat:
                          @"query=%@&artistCount=0&albumCount=500&songCount=0",
                          urlEncode(artistName)];
        std::vector<navidrome::Album> searchAlbums;
        for (NSString *fid in folderIds) {
            NSURL *sUrl = [self urlForEndpoint:@"search3.view"
                                        params:appendMusicFolder(base, fid)];
            NSError *ignored = nil;
            navidrome::json::Value sRoot = [self fetchInner:sUrl error:&ignored];
            if (sRoot.isNull()) continue;
            for (auto *a : sRoot["searchResult3"]["album"].items())
                searchAlbums.push_back(navidrome::parseAlbum(*a));
        }
        bool unconfirmed = false;
        albums = navidrome::filterAlbumsByArtistSearch(albums, artistIdStr,
                                                       searchAlbums, unconfirmed);
        if (unconfirmed) {
            NAVIDROME_WARN("HTTP", "library filter: could not confirm album membership "
                           "for artist " + artistIdStr + " — showing all albums");
        }
    }

    NSMutableArray<SubsonicAlbum *> *result =
        [NSMutableArray arrayWithCapacity:albums.size()];
    for (const auto &al : albums) [result addObject:AlbumFromCore(al)];
    return result;
}

- (NSArray<SubsonicSong *> *)getSongsForAlbum:(NSString *)albumId error:(NSError **)error {
    NSString *params = [NSString stringWithFormat:@"id=%@", urlEncode(albumId)];
    NSURL *url = [self urlForEndpoint:@"getAlbum.view" params:params];
    navidrome::json::Value root = [self fetchInner:url error:error];
    if (root.isNull()) return nil;

    NSMutableArray<SubsonicSong *> *result = [NSMutableArray array];
    for (auto *s : root["album"]["song"].items()) {
        SubsonicSong *song = SongFromCore(navidrome::parseSong(*s));
        if (song.albumId.length == 0) song.albumId = albumId;
        [result addObject:song];
    }

    return result;
}

- (NSDictionary *)search:(NSString *)query error:(NSError **)error {
    NSString *base = [NSString stringWithFormat:@"query=%@&artistCount=20&albumCount=20&songCount=50",
                      urlEncode(query)];

    NSArray<NSString *> *folderIds = [self activeMusicFolderIds];
    NSArray<NSString *> *passes = folderIds.count ? folderIds : @[ [NSNull null] ];

    NSMutableArray<SubsonicArtist *> *artists = [NSMutableArray array];
    NSMutableArray<SubsonicAlbum *>  *albums  = [NSMutableArray array];
    NSMutableArray<SubsonicSong *>   *songs   = [NSMutableArray array];
    NSMutableSet<NSString *> *seenArtists = [NSMutableSet set];
    NSMutableSet<NSString *> *seenAlbums  = [NSMutableSet set];
    NSMutableSet<NSString *> *seenSongs   = [NSMutableSet set];

    for (id pass in passes) {
        NSString *folderId = [pass isKindOfClass:[NSString class]] ? pass : nil;
        NSURL *url = [self urlForEndpoint:@"search3.view"
                                   params:appendMusicFolder(base, folderId)];
        navidrome::json::Value root = [self fetchInner:url error:error];
        if (root.isNull()) {
            if (artists.count || albums.count || songs.count) break;  // keep partials
            return nil;
        }
        const navidrome::json::Value &searchResult = root["searchResult3"];

        for (auto *a : searchResult["artist"].items()) {
            SubsonicArtist *artist = ArtistFromCore(navidrome::parseArtist(*a));
            if (artist.artistId.length && [seenArtists containsObject:artist.artistId]) continue;
            if (artist.artistId.length) [seenArtists addObject:artist.artistId];
            [artists addObject:artist];
        }
        for (auto *a : searchResult["album"].items()) {
            SubsonicAlbum *album = AlbumFromCore(navidrome::parseAlbum(*a));
            if (album.albumId.length && [seenAlbums containsObject:album.albumId]) continue;
            if (album.albumId.length) [seenAlbums addObject:album.albumId];
            [albums addObject:album];
        }
        for (auto *s : searchResult["song"].items()) {
            SubsonicSong *song = SongFromCore(navidrome::parseSong(*s));
            if (song.songId.length && [seenSongs containsObject:song.songId]) continue;
            if (song.songId.length) [seenSongs addObject:song.songId];
            [songs addObject:song];
        }
    }

    return @{ @"artists": artists, @"albums": albums, @"songs": songs };
}

// ---------------------------------------------------------------------------
// Smart lists, favorites, ratings, playlists, scrobbling
// ---------------------------------------------------------------------------

- (NSArray<SubsonicAlbum *> *)getAlbumListOfType:(NSString *)type
                                            size:(NSInteger)size
                                           error:(NSError **)error {
    NSString *base = [NSString stringWithFormat:@"type=%@&size=%ld",
                      urlEncode(type), (long)size];
    return [self fanOutOverFolders:[self activeMusicFolderIds]
                             idKey:@"albumId"
                             fetch:^NSArray *(NSString *folderId) {
        NSURL *url = [self urlForEndpoint:@"getAlbumList2.view"
                                   params:appendMusicFolder(base, folderId)];
        navidrome::json::Value root = [self fetchInner:url error:error];
        if (root.isNull()) return nil;

        NSMutableArray<SubsonicAlbum *> *result = [NSMutableArray array];
        for (auto *a : root["albumList2"]["album"].items())
            [result addObject:AlbumFromCore(navidrome::parseAlbum(*a))];
        return result;
    }];
}

- (NSArray<SubsonicSong *> *)getStarredSongsWithError:(NSError **)error {
    return [self fanOutOverFolders:[self activeMusicFolderIds]
                             idKey:@"songId"
                             fetch:^NSArray *(NSString *folderId) {
        NSURL *url = [self urlForEndpoint:@"getStarred2.view"
                                   params:appendMusicFolder(@"", folderId)];
        navidrome::json::Value root = [self fetchInner:url error:error];
        if (root.isNull()) return nil;

        NSMutableArray<SubsonicSong *> *result = [NSMutableArray array];
        for (auto *s : root["starred2"]["song"].items()) {
            SubsonicSong *song = SongFromCore(navidrome::parseSong(*s));
            song.starred = YES;   // getStarred2 omits the "starred" field per item
            [result addObject:song];
        }
        return result;
    }];
}

- (NSArray<SubsonicGenre *> *)getGenresWithError:(NSError **)error {
    NSURL *url = [self urlForEndpoint:@"getGenres.view" params:@""];
    navidrome::json::Value root = [self fetchInner:url error:error];
    if (root.isNull()) return nil;

    NSMutableArray<SubsonicGenre *> *result = [NSMutableArray array];
    for (auto *g : root["genres"]["genre"].items()) {
        SubsonicGenre *genre = GenreFromCore(navidrome::parseGenre(*g));
        // Skip the empty "no genre" bucket some servers report.
        if (genre.name.length == 0) continue;
        [result addObject:genre];
    }
    return result;
}

- (NSArray<SubsonicSong *> *)getSongsForGenre:(NSString *)genre
                                        count:(NSInteger)count
                                        error:(NSError **)error {
    if (genre.length == 0) return @[];
    NSString *base = [NSString stringWithFormat:@"genre=%@&count=%ld",
                      urlEncode(genre), (long)count];
    return [self fanOutOverFolders:[self activeMusicFolderIds]
                             idKey:@"songId"
                             fetch:^NSArray *(NSString *folderId) {
        NSURL *url = [self urlForEndpoint:@"getSongsByGenre.view"
                                   params:appendMusicFolder(base, folderId)];
        navidrome::json::Value root = [self fetchInner:url error:error];
        if (root.isNull()) return nil;

        NSMutableArray<SubsonicSong *> *result = [NSMutableArray array];
        for (auto *s : root["songsByGenre"]["song"].items())
            [result addObject:SongFromCore(navidrome::parseSong(*s))];
        return result;
    }];
}

- (NSArray<SubsonicSong *> *)getSimilarSongsForId:(NSString *)itemId
                                             count:(NSInteger)count
                                             error:(NSError **)error {
    if (itemId.length == 0) return @[];
    NSString *params = [NSString stringWithFormat:@"id=%@&count=%ld",
                        urlEncode(itemId), (long)count];
    NSURL *url = [self urlForEndpoint:@"getSimilarSongs2.view" params:params];
    navidrome::json::Value root = [self fetchInner:url error:error];
    if (root.isNull()) return nil;

    NSMutableArray<SubsonicSong *> *result = [NSMutableArray array];
    for (auto *s : root["similarSongs2"]["song"].items())
        [result addObject:SongFromCore(navidrome::parseSong(*s))];
    return result;
}

- (NSArray<SubsonicSong *> *)getRandomSongsWithCount:(NSInteger)count
                                                error:(NSError **)error {
    NSArray<NSString *> *folderIds = [self activeMusicFolderIds];
    // Split the requested size across the fanned-out libraries so the merged
    // result stays near `count` rather than count-per-library.
    NSInteger perFolder = folderIds.count
        ? MAX(1, count / (NSInteger)folderIds.count + 1)
        : count;

    NSArray *merged = [self fanOutOverFolders:folderIds
                                       idKey:@"songId"
                                       fetch:^NSArray *(NSString *folderId) {
        NSString *params = appendMusicFolder(
            [NSString stringWithFormat:@"size=%ld", (long)perFolder], folderId);
        NSURL *url = [self urlForEndpoint:@"getRandomSongs.view" params:params];
        navidrome::json::Value root = [self fetchInner:url error:error];
        if (root.isNull()) return nil;

        NSMutableArray<SubsonicSong *> *result = [NSMutableArray array];
        for (auto *s : root["randomSongs"]["song"].items())
            [result addObject:SongFromCore(navidrome::parseSong(*s))];
        return result;
    }];

    if (folderIds.count && (NSInteger)merged.count > count)
        merged = [merged subarrayWithRange:NSMakeRange(0, count)];
    return merged;
}

- (BOOL)setStarred:(BOOL)starred
             forId:(NSString *)itemId
              kind:(SubsonicStarKind)kind
             error:(NSError **)error {
    if (itemId.length == 0) return NO;
    // Subsonic names the parameter after the item kind.
    NSString *param = @"id";
    if (kind == SubsonicStarKindAlbum)  param = @"albumId";
    if (kind == SubsonicStarKindArtist) param = @"artistId";

    NSString *params = [NSString stringWithFormat:@"%@=%@", param, urlEncode(itemId)];
    NSURL *url = [self urlForEndpoint:(starred ? @"star.view" : @"unstar.view")
                               params:params];
    return ![self fetchInner:url error:error].isNull();
}

- (SubsonicSong *)getSongWithId:(NSString *)songId error:(NSError **)error {
    if (songId.length == 0) return nil;
    NSString *params = [NSString stringWithFormat:@"id=%@", urlEncode(songId)];
    NSURL *url = [self urlForEndpoint:@"getSong.view" params:params];
    navidrome::json::Value root = [self fetchInner:url error:error];
    if (root.isNull()) return nil;
    auto songs = root["song"].items();
    return songs.empty() ? nil : SongFromCore(navidrome::parseSong(*songs.front()));
}

- (BOOL)setRating:(NSInteger)rating forSongId:(NSString *)songId error:(NSError **)error {
    if (songId.length == 0) return NO;
    NSString *params = [NSString stringWithFormat:@"id=%@&rating=%ld",
                        urlEncode(songId), (long)MAX(0, MIN(5, rating))];
    NSURL *url = [self urlForEndpoint:@"setRating.view" params:params];
    return ![self fetchInner:url error:error].isNull();
}

- (NSArray<SubsonicPlaylist *> *)getPlaylistsWithError:(NSError **)error {
    NSURL *url = [self urlForEndpoint:@"getPlaylists.view" params:@""];
    navidrome::json::Value root = [self fetchInner:url error:error];
    if (root.isNull()) return nil;

    NSMutableArray<SubsonicPlaylist *> *result = [NSMutableArray array];
    for (auto *p : root["playlists"]["playlist"].items())
        [result addObject:PlaylistFromCore(navidrome::parsePlaylist(*p))];
    return result;
}

- (NSArray<SubsonicSong *> *)getPlaylistSongs:(NSString *)playlistId error:(NSError **)error {
    NSString *params = [NSString stringWithFormat:@"id=%@", urlEncode(playlistId)];
    NSURL *url = [self urlForEndpoint:@"getPlaylist.view" params:params];
    navidrome::json::Value root = [self fetchInner:url error:error];
    if (root.isNull()) return nil;

    NSMutableArray<SubsonicSong *> *result = [NSMutableArray array];
    for (auto *s : root["playlist"]["entry"].items())
        [result addObject:SongFromCore(navidrome::parseSong(*s))];
    return result;
}

// Subsonic passes track ids on the query string, so a long playlist would blow
// past typical server URL limits — create with the first chunk, then grow it
// with updatePlaylist.view calls. Returns the new playlist's id.
- (NSString *)createPlaylistNamed:(NSString *)name
                          songIds:(NSArray<NSString *> *)songIds
                            error:(NSError **)error {
    if (name.length == 0) return nil;
    const NSUInteger kChunk = navidrome::kPlaylistChunkSize;

    NSUInteger first = MIN(kChunk, songIds.count);
    NSMutableString *params = [NSMutableString stringWithFormat:@"name=%@", urlEncode(name)];
    for (NSUInteger i = 0; i < first; i++)
        [params appendFormat:@"&songId=%@", urlEncode(songIds[i])];

    navidrome::json::Value root = [self fetchInner:[self urlForEndpoint:@"createPlaylist.view"
                                                       params:params]
                                   error:error];
    if (root.isNull()) return nil;

    // Navidrome echoes the created playlist back; without its id the remaining
    // tracks can't be appended (and the caller can't act on the new playlist).
    auto created = root["playlist"].items();
    NSString *playlistId = created.empty() ? @"" : nsstr(navidrome::jId(*created[0], "id"));
    if (playlistId.length == 0) {
        if (songIds.count <= kChunk) {
            // Everything made it in; we just don't have an id to hand back.
            // Report success with an empty id rather than a phantom failure.
            return @"";
        }
        if (error) {
            *error = [NSError errorWithDomain:@"SubsonicClient" code:-3 userInfo:@{
                NSLocalizedDescriptionKey: [NSString stringWithFormat:
                    @"Playlist created, but the server returned no id — only the "
                     "first %lu tracks were added", (unsigned long)kChunk]}];
        }
        return nil;
    }

    if (songIds.count <= kChunk) return playlistId;

    NSArray<NSString *> *rest = [songIds subarrayWithRange:
        NSMakeRange(kChunk, songIds.count - kChunk)];
    if (![self addSongs:rest toPlaylist:playlistId error:error]) return nil;
    return playlistId;
}

- (BOOL)addSongs:(NSArray<NSString *> *)songIds
      toPlaylist:(NSString *)playlistId
           error:(NSError **)error {
    if (playlistId.length == 0 || songIds.count == 0) return NO;
    const NSUInteger kChunk = navidrome::kPlaylistChunkSize;
    const NSUInteger chunks = (songIds.count + kChunk - 1) / kChunk;
    NAVIDROME_LOG("Playlist", "add " + std::to_string((unsigned long)songIds.count) +
                  " ids to " + (playlistId.UTF8String ?: "?") + " in " +
                  std::to_string((unsigned long)chunks) + " chunk(s)");

    for (NSUInteger i = 0, c = 1; i < songIds.count; i += kChunk, c++) {
        NSMutableString *upd = [NSMutableString stringWithFormat:@"playlistId=%@",
                                urlEncode(playlistId)];
        for (NSUInteger j = i; j < MIN(i + kChunk, songIds.count); j++)
            [upd appendFormat:@"&songIdToAdd=%@", urlEncode(songIds[j])];
        if ([self fetchInner:[self urlForEndpoint:@"updatePlaylist.view" params:upd]
                        error:error].isNull()) {
            NAVIDROME_ERR("Playlist", "add: chunk " + std::to_string((unsigned long)c) + "/" +
                          std::to_string((unsigned long)chunks) + " failed after " +
                          std::to_string((unsigned long)i) + "/" +
                          std::to_string((unsigned long)songIds.count) + " ids: " +
                          _lastError.kindName());
            return NO;
        }
    }
    return YES;
}

// songIndexToRemove refers to a track's position in the playlist as it stands
// when the request is served, so removals are sent highest-index-first: dropping
// a later entry never shifts an earlier one.
- (BOOL)removeIndexes:(NSArray<NSNumber *> *)indexes
         fromPlaylist:(NSString *)playlistId
                error:(NSError **)error {
    if (playlistId.length == 0 || indexes.count == 0) return NO;
    const NSUInteger kChunk = navidrome::kPlaylistChunkSize;

    NSArray<NSNumber *> *sorted = [indexes sortedArrayUsingComparator:
        ^NSComparisonResult(NSNumber *a, NSNumber *b) { return [b compare:a]; }];
    const NSUInteger chunks = (sorted.count + kChunk - 1) / kChunk;
    NAVIDROME_LOG("Playlist", "remove " + std::to_string((unsigned long)sorted.count) +
                  " index(es) from " + (playlistId.UTF8String ?: "?") +
                  " (highest-first) in " + std::to_string((unsigned long)chunks) + " chunk(s)");

    for (NSUInteger i = 0, c = 1; i < sorted.count; i += kChunk, c++) {
        NSMutableString *upd = [NSMutableString stringWithFormat:@"playlistId=%@",
                                urlEncode(playlistId)];
        for (NSUInteger j = i; j < MIN(i + kChunk, sorted.count); j++)
            [upd appendFormat:@"&songIndexToRemove=%ld", (long)sorted[j].integerValue];
        if ([self fetchInner:[self urlForEndpoint:@"updatePlaylist.view" params:upd]
                        error:error].isNull()) {
            NAVIDROME_ERR("Playlist", "remove: chunk " + std::to_string((unsigned long)c) + "/" +
                          std::to_string((unsigned long)chunks) + " failed after " +
                          std::to_string((unsigned long)i) + "/" +
                          std::to_string((unsigned long)sorted.count) + " indexes: " +
                          _lastError.kindName());
            return NO;
        }
    }
    return YES;
}

- (BOOL)renamePlaylist:(NSString *)playlistId
                toName:(NSString *)name
                 error:(NSError **)error {
    if (playlistId.length == 0 || name.length == 0) return NO;
    NSString *params = [NSString stringWithFormat:@"playlistId=%@&name=%@",
                        urlEncode(playlistId), urlEncode(name)];
    return ![self fetchInner:[self urlForEndpoint:@"updatePlaylist.view" params:params]
                      error:error].isNull();
}

- (BOOL)deletePlaylist:(NSString *)playlistId error:(NSError **)error {
    if (playlistId.length == 0) return NO;
    NSString *params = [NSString stringWithFormat:@"id=%@", urlEncode(playlistId)];
    return ![self fetchInner:[self urlForEndpoint:@"deletePlaylist.view" params:params]
                      error:error].isNull();
}

- (NSArray<SubsonicRadioStation *> *)getRadioStationsWithError:(NSError **)error {
    NSURL *url = [self urlForEndpoint:@"getInternetRadioStations.view" params:@""];
    navidrome::json::Value root = [self fetchInner:url error:error];
    if (root.isNull()) return nil;

    NSMutableArray<SubsonicRadioStation *> *result = [NSMutableArray array];
    for (auto *s : root["internetRadioStations"]["internetRadioStation"].items())
        [result addObject:RadioStationFromCore(navidrome::parseRadioStation(*s))];
    return result;
}

- (NSString *)createRadioStationWithStreamURL:(NSString *)streamUrl
                                          name:(NSString *)name
                                   homePageUrl:(NSString *)homePageUrl
                                         error:(NSError **)error {
    if (streamUrl.length == 0 || name.length == 0) return nil;
    NSMutableString *params = [NSMutableString stringWithFormat:@"streamUrl=%@&name=%@",
                               urlEncode(streamUrl), urlEncode(name)];
    if (homePageUrl.length > 0)
        [params appendFormat:@"&homePageUrl=%@", urlEncode(homePageUrl)];

    navidrome::json::Value root = [self fetchInner:[self urlForEndpoint:@"createInternetRadioStation.view"
                                                       params:params]
                                   error:error];
    if (root.isNull()) return nil;
    // Unlike createPlaylist.view, Subsonic's create-station endpoint doesn't
    // echo the new station's id back. Report success with an empty id rather
    // than a phantom failure — callers must check *error, not this string.
    return @"";
}

- (BOOL)updateRadioStation:(NSString *)stationId
                  streamURL:(NSString *)streamUrl
                       name:(NSString *)name
                homePageUrl:(NSString *)homePageUrl
                      error:(NSError **)error {
    if (stationId.length == 0 || streamUrl.length == 0 || name.length == 0) return NO;
    NSMutableString *params = [NSMutableString stringWithFormat:@"id=%@&streamUrl=%@&name=%@",
                               urlEncode(stationId), urlEncode(streamUrl), urlEncode(name)];
    if (homePageUrl.length > 0)
        [params appendFormat:@"&homePageUrl=%@", urlEncode(homePageUrl)];
    return ![self fetchInner:[self urlForEndpoint:@"updateInternetRadioStation.view" params:params]
                      error:error].isNull();
}

- (BOOL)deleteRadioStation:(NSString *)stationId error:(NSError **)error {
    if (stationId.length == 0) return NO;
    NSString *params = [NSString stringWithFormat:@"id=%@", urlEncode(stationId)];
    return ![self fetchInner:[self urlForEndpoint:@"deleteInternetRadioStation.view" params:params]
                      error:error].isNull();
}

- (NSArray<SubsonicBookmark *> *)getBookmarksWithError:(NSError **)error {
    NSURL *url = [self urlForEndpoint:@"getBookmarks.view" params:@""];
    navidrome::json::Value root = [self fetchInner:url error:error];
    if (root.isNull()) return nil;

    NSMutableArray<SubsonicBookmark *> *result = [NSMutableArray array];
    for (auto *b : root["bookmarks"]["bookmark"].items()) {
        navidrome::Bookmark bm;
        if (navidrome::parseBookmark(*b, bm))
            [result addObject:BookmarkFromCore(bm)];
    }
    return result;
}

- (BOOL)createBookmarkForSongId:(NSString *)songId
                      positionMs:(NSTimeInterval)positionMs
                         comment:(NSString *)comment
                           error:(NSError **)error {
    if (songId.length == 0) return NO;
    NSMutableString *params = [NSMutableString stringWithFormat:@"id=%@&position=%lld",
                               urlEncode(songId), (long long)positionMs];
    if (comment.length > 0) [params appendFormat:@"&comment=%@", urlEncode(comment)];
    return ![self fetchInner:[self urlForEndpoint:@"createBookmark.view" params:params]
                      error:error].isNull();
}

- (BOOL)deleteBookmarkForSongId:(NSString *)songId error:(NSError **)error {
    if (songId.length == 0) return NO;
    NSString *params = [NSString stringWithFormat:@"id=%@", urlEncode(songId)];
    return ![self fetchInner:[self urlForEndpoint:@"deleteBookmark.view" params:params]
                      error:error].isNull();
}

- (BOOL)fetchScanStatusForEndpoint:(NSString *)endpoint
                           scanning:(BOOL *)scanning
                              count:(NSInteger *)count
                              error:(NSError **)error {
    if (scanning) *scanning = NO;
    if (count)    *count    = 0;
    NSURL *url = [self urlForEndpoint:endpoint params:@""];
    navidrome::json::Value root = [self fetchInner:url error:error];
    if (root.isNull()) return NO;
    navidrome::ScanStatus st = navidrome::parseScanStatus(root);
    if (scanning) *scanning = st.scanning;
    if (count)    *count    = static_cast<NSInteger>(st.count);
    return YES;
}

- (BOOL)startScanWithScanning:(BOOL *)scanning count:(NSInteger *)count error:(NSError **)error {
    return [self fetchScanStatusForEndpoint:@"startScan.view"
                                    scanning:scanning count:count error:error];
}

- (BOOL)getScanStatusWithScanning:(BOOL *)scanning count:(NSInteger *)count error:(NSError **)error {
    return [self fetchScanStatusForEndpoint:@"getScanStatus.view"
                                    scanning:scanning count:count error:error];
}

- (BOOL)scrobbleSongId:(NSString *)songId
            submission:(BOOL)submission
                 error:(NSError **)error {
    if (songId.length == 0) return NO;
    NSString *params = [NSString stringWithFormat:@"id=%@&submission=%@",
                        urlEncode(songId), submission ? @"true" : @"false"];
    NSURL *url = [self urlForEndpoint:@"scrobble.view" params:params];
    return ![self fetchInner:url error:error].isNull();
}

// ---------------------------------------------------------------------------
// URL builders
// ---------------------------------------------------------------------------

- (NSString *)streamURLForSongId:(NSString *)songId coverArtId:(NSString *)coverArtId {
    NSString *base = [NSString stringWithUTF8String:navidrome::cfg_server_url.get().c_str()];
    while ([base hasSuffix:@"/"]) base = [base substringToIndex:base.length - 1];
    NSString *auth = [self authParams];
    NSString *artParam = (coverArtId.length > 0)
        ? [NSString stringWithFormat:@"&coverArt=%@", urlEncode(coverArtId)]
        : @"";
    // Transcoding preferences — the server falls back to its own defaults when
    // neither is set.
    std::string transcode = navidrome::streamTranscodeParams(
        navidrome::cfg_stream_format.get().c_str(),
        static_cast<int>(navidrome::cfg_max_bitrate.get()));
    return [NSString stringWithFormat:@"%@/rest/stream.view?id=%@%@&%@%s",
            base, urlEncode(songId), artParam, auth, transcode.c_str()];
}

- (NSURL *)downloadURLForSongId:(NSString *)songId {
    NSString *params = [NSString stringWithFormat:@"id=%@", urlEncode(songId)];
    return [self urlForEndpoint:@"download.view" params:params];
}

- (NSURL *)coverArtURLForId:(NSString *)coverArtId size:(NSInteger)size {
    NSString *base = [NSString stringWithUTF8String:navidrome::cfg_server_url.get().c_str()];
    while ([base hasSuffix:@"/"]) base = [base substringToIndex:base.length - 1];
    NSString *auth = [self authParams];
    NSString *sizeParam = size > 0 ? [NSString stringWithFormat:@"&size=%ld", (long)size] : @"";
    NSString *full = [NSString stringWithFormat:@"%@/rest/getCoverArt.view?id=%@&%@%@",
                      base, urlEncode(coverArtId), auth, sizeParam];
    return [NSURL URLWithString:full];
}

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
