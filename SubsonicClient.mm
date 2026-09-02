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

// Subsonic's JSON collapses a single-element array into a bare object, so every
// list field has to be normalized before iterating.
static NSArray *asArray(id value) {
    if ([value isKindOfClass:[NSArray class]])      return value;
    if ([value isKindOfClass:[NSDictionary class]]) return @[value];
    return @[];
}

static SubsonicSong *parseSong(NSDictionary *s) {
    SubsonicSong *song = [[SubsonicSong alloc] init];
    song.songId     = s[@"id"] ?: @"";
    song.title      = s[@"title"] ?: @"Unknown Title";
    song.artist     = s[@"artist"] ?: @"";
    song.artistId   = s[@"artistId"] ?: @"";
    song.album      = s[@"album"] ?: @"";
    song.albumId    = s[@"albumId"] ?: @"";
    song.track      = [s[@"track"] integerValue];
    song.year       = [s[@"year"] integerValue];
    song.duration   = [s[@"duration"] doubleValue];
    song.coverArtId = s[@"coverArt"] ?: @"";
    song.suffix     = s[@"suffix"] ?: @"";
    song.starred    = s[@"starred"] != nil;
    song.rating     = [s[@"userRating"] integerValue];
    return song;
}

static SubsonicAlbum *parseAlbum(NSDictionary *a) {
    SubsonicAlbum *album = [[SubsonicAlbum alloc] init];
    album.albumId    = a[@"id"] ?: @"";
    album.name       = a[@"name"] ?: @"Unknown Album";
    album.artist     = a[@"artist"] ?: @"";
    album.artistId   = a[@"artistId"] ?: @"";
    album.songCount  = [a[@"songCount"] integerValue];
    album.year       = [a[@"year"] integerValue];
    album.coverArtId = a[@"coverArt"] ?: @"";
    album.starred    = a[@"starred"] != nil;
    return album;
}

// ---------------------------------------------------------------------------
// SubsonicClient
// ---------------------------------------------------------------------------

@interface SubsonicClient ()
@property (nonatomic, strong) NSURLSession *session;
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

// Synchronous HTTP GET, returns parsed JSON or nil
- (NSDictionary *)fetchJSON:(NSURL *)url error:(NSError **)outError {
    __block NSData *responseData = nil;
    __block NSError *taskError = nil;
    __block NSHTTPURLResponse *httpResponse = nil;

    NSMutableURLRequest *request = [NSMutableURLRequest requestWithURL:url];
    NavidromeApplyCustomHeaders(request);
    NAVIDROME_LOG("HTTP", "GET " + navidrome::dbg::scrubAuth(std::string(url.absoluteString.UTF8String ?: "")));

    dispatch_semaphore_t sema = dispatch_semaphore_create(0);
    [[_session dataTaskWithRequest:request completionHandler:^(NSData *data, NSURLResponse *response, NSError *error) {
        responseData = data;
        taskError = error;
        httpResponse = (NSHTTPURLResponse *)response;
        dispatch_semaphore_signal(sema);
    }] resume];
    dispatch_semaphore_wait(sema, DISPATCH_TIME_FOREVER);

    if (taskError) {
        NAVIDROME_ERR("HTTP", std::string("network error: ") + (taskError.localizedDescription.UTF8String ?: "?"));
        if (outError) *outError = taskError;
        return nil;
    }
    if (httpResponse.statusCode != 200) {
        NAVIDROME_ERR("HTTP", "HTTP " + std::to_string((long)httpResponse.statusCode));
        if (outError) {
            *outError = [NSError errorWithDomain:@"SubsonicClient"
                                           code:httpResponse.statusCode
                                       userInfo:@{NSLocalizedDescriptionKey:
                                                  [NSString stringWithFormat:@"HTTP %ld", (long)httpResponse.statusCode]}];
        }
        return nil;
    }
    if (!responseData) {
        NAVIDROME_ERR("HTTP", "empty response body");
        if (outError) {
            *outError = [NSError errorWithDomain:@"SubsonicClient" code:-1
                                       userInfo:@{NSLocalizedDescriptionKey: @"Empty response"}];
        }
        return nil;
    }

    NSError *jsonError = nil;
    NSDictionary *json = [NSJSONSerialization JSONObjectWithData:responseData options:0 error:&jsonError];
    if (!json || jsonError) {
        NAVIDROME_ERR("HTTP", std::string("JSON parse failed: ") + (jsonError.localizedDescription.UTF8String ?: "?"));
        if (outError) *outError = jsonError;
        return nil;
    }

    // Subsonic wraps everything in "subsonic-response"
    NSDictionary *root = json[@"subsonic-response"];
    if (!root) {
        NAVIDROME_ERR("API", "response missing subsonic-response wrapper");
        if (outError) {
            *outError = [NSError errorWithDomain:@"SubsonicClient" code:-2
                                       userInfo:@{NSLocalizedDescriptionKey: @"Invalid response format"}];
        }
        return nil;
    }

    NSString *status = root[@"status"];
    if (![status isEqualToString:@"ok"]) {
        NSDictionary *err = root[@"error"];
        NSString *msg = err[@"message"] ?: @"Unknown Subsonic error";
        NAVIDROME_ERR("API", std::string("Subsonic status != ok: ") + (msg.UTF8String ?: "?"));
        if (outError) {
            *outError = [NSError errorWithDomain:@"SubsonicClient"
                                           code:[err[@"code"] integerValue]
                                       userInfo:@{NSLocalizedDescriptionKey: msg}];
        }
        return nil;
    }

    NAVIDROME_LOG("HTTP", "200 OK  " + std::to_string((unsigned long)responseData.length) + " bytes");
    return root;
}

// ---------------------------------------------------------------------------
// API Methods
// ---------------------------------------------------------------------------

- (BOOL)pingWithError:(NSError **)error {
    NSURL *url = [self urlForEndpoint:@"ping.view" params:@""];
    NSDictionary *root = [self fetchJSON:url error:error];
    return root != nil;
}

- (NSArray<SubsonicArtist *> *)getArtistsWithError:(NSError **)error {
    NSURL *url = [self urlForEndpoint:@"getArtists.view" params:@""];
    NSDictionary *root = [self fetchJSON:url error:error];
    if (!root) return nil;

    NSMutableArray<SubsonicArtist *> *result = [NSMutableArray array];
    NSDictionary *artistsObj = root[@"artists"];
    NSArray *indexArray = artistsObj[@"index"];

    for (NSDictionary *index in indexArray) {
        NSArray *artists = index[@"artist"];
        if (![artists isKindOfClass:[NSArray class]]) {
            // Single artist returned as dict
            if ([artists isKindOfClass:[NSDictionary class]]) {
                artists = @[(NSDictionary *)artists];
            } else {
                continue;
            }
        }
        for (NSDictionary *a in artists) {
            SubsonicArtist *artist = [[SubsonicArtist alloc] init];
            artist.artistId  = a[@"id"] ?: @"";
            artist.name      = a[@"name"] ?: @"Unknown Artist";
            artist.albumCount = [a[@"albumCount"] integerValue];
            artist.coverArtId = a[@"coverArt"] ?: @"";
            artist.starred    = a[@"starred"] != nil;
            [result addObject:artist];
        }
    }

    return result;
}

- (NSArray<SubsonicAlbum *> *)getAlbumsForArtist:(NSString *)artistId error:(NSError **)error {
    NSString *params = [NSString stringWithFormat:@"id=%@", urlEncode(artistId)];
    NSURL *url = [self urlForEndpoint:@"getArtist.view" params:params];
    NSDictionary *root = [self fetchJSON:url error:error];
    if (!root) return nil;

    NSMutableArray<SubsonicAlbum *> *result = [NSMutableArray array];
    NSDictionary *artistObj = root[@"artist"];
    for (NSDictionary *a in asArray(artistObj[@"album"])) {
        SubsonicAlbum *album = parseAlbum(a);
        if (album.artistId.length == 0) album.artistId = artistId;
        [result addObject:album];
    }

    return result;
}

- (NSArray<SubsonicSong *> *)getSongsForAlbum:(NSString *)albumId error:(NSError **)error {
    NSString *params = [NSString stringWithFormat:@"id=%@", urlEncode(albumId)];
    NSURL *url = [self urlForEndpoint:@"getAlbum.view" params:params];
    NSDictionary *root = [self fetchJSON:url error:error];
    if (!root) return nil;

    NSMutableArray<SubsonicSong *> *result = [NSMutableArray array];
    NSDictionary *albumObj = root[@"album"];
    for (NSDictionary *s in asArray(albumObj[@"song"])) {
        SubsonicSong *song = parseSong(s);
        if (song.albumId.length == 0) song.albumId = albumId;
        [result addObject:song];
    }

    return result;
}

- (NSDictionary *)search:(NSString *)query error:(NSError **)error {
    NSString *params = [NSString stringWithFormat:@"query=%@&artistCount=20&albumCount=20&songCount=50",
                        urlEncode(query)];
    NSURL *url = [self urlForEndpoint:@"search3.view" params:params];
    NSDictionary *root = [self fetchJSON:url error:error];
    if (!root) return nil;

    NSDictionary *searchResult = root[@"searchResult3"] ?: @{};

    // Parse artists
    NSMutableArray<SubsonicArtist *> *artists = [NSMutableArray array];
    for (NSDictionary *a in asArray(searchResult[@"artist"])) {
        SubsonicArtist *artist = [[SubsonicArtist alloc] init];
        artist.artistId  = a[@"id"] ?: @"";
        artist.name      = a[@"name"] ?: @"";
        artist.coverArtId = a[@"coverArt"] ?: @"";
        artist.starred   = a[@"starred"] != nil;
        [artists addObject:artist];
    }

    // Parse albums
    NSMutableArray<SubsonicAlbum *> *albums = [NSMutableArray array];
    for (NSDictionary *a in asArray(searchResult[@"album"]))
        [albums addObject:parseAlbum(a)];

    // Parse songs
    NSMutableArray<SubsonicSong *> *songs = [NSMutableArray array];
    for (NSDictionary *s in asArray(searchResult[@"song"]))
        [songs addObject:parseSong(s)];

    return @{ @"artists": artists, @"albums": albums, @"songs": songs };
}

// ---------------------------------------------------------------------------
// Smart lists, favorites, ratings, playlists, scrobbling
// ---------------------------------------------------------------------------

- (NSArray<SubsonicAlbum *> *)getAlbumListOfType:(NSString *)type
                                            size:(NSInteger)size
                                           error:(NSError **)error {
    NSString *params = [NSString stringWithFormat:@"type=%@&size=%ld",
                        urlEncode(type), (long)size];
    NSURL *url = [self urlForEndpoint:@"getAlbumList2.view" params:params];
    NSDictionary *root = [self fetchJSON:url error:error];
    if (!root) return nil;

    NSMutableArray<SubsonicAlbum *> *result = [NSMutableArray array];
    for (NSDictionary *a in asArray(root[@"albumList2"][@"album"]))
        [result addObject:parseAlbum(a)];
    return result;
}

- (NSArray<SubsonicSong *> *)getStarredSongsWithError:(NSError **)error {
    NSURL *url = [self urlForEndpoint:@"getStarred2.view" params:@""];
    NSDictionary *root = [self fetchJSON:url error:error];
    if (!root) return nil;

    NSMutableArray<SubsonicSong *> *result = [NSMutableArray array];
    for (NSDictionary *s in asArray(root[@"starred2"][@"song"])) {
        SubsonicSong *song = parseSong(s);
        song.starred = YES;   // getStarred2 omits the "starred" field per item
        [result addObject:song];
    }
    return result;
}

- (NSArray<SubsonicGenre *> *)getGenresWithError:(NSError **)error {
    NSURL *url = [self urlForEndpoint:@"getGenres.view" params:@""];
    NSDictionary *root = [self fetchJSON:url error:error];
    if (!root) return nil;

    NSMutableArray<SubsonicGenre *> *result = [NSMutableArray array];
    for (NSDictionary *g in asArray(root[@"genres"][@"genre"])) {
        // Subsonic puts the genre name in "value"; skip the empty "no genre"
        // bucket some servers report.
        NSString *name = g[@"value"] ?: @"";
        if (name.length == 0) continue;
        SubsonicGenre *genre = [[SubsonicGenre alloc] init];
        genre.name       = name;
        genre.songCount  = [g[@"songCount"] integerValue];
        genre.albumCount = [g[@"albumCount"] integerValue];
        [result addObject:genre];
    }
    return result;
}

- (NSArray<SubsonicSong *> *)getSongsForGenre:(NSString *)genre
                                        count:(NSInteger)count
                                        error:(NSError **)error {
    if (genre.length == 0) return @[];
    NSString *params = [NSString stringWithFormat:@"genre=%@&count=%ld",
                        urlEncode(genre), (long)count];
    NSURL *url = [self urlForEndpoint:@"getSongsByGenre.view" params:params];
    NSDictionary *root = [self fetchJSON:url error:error];
    if (!root) return nil;

    NSMutableArray<SubsonicSong *> *result = [NSMutableArray array];
    for (NSDictionary *s in asArray(root[@"songsByGenre"][@"song"]))
        [result addObject:parseSong(s)];
    return result;
}

- (NSArray<SubsonicSong *> *)getSimilarSongsForId:(NSString *)itemId
                                             count:(NSInteger)count
                                             error:(NSError **)error {
    if (itemId.length == 0) return @[];
    NSString *params = [NSString stringWithFormat:@"id=%@&count=%ld",
                        urlEncode(itemId), (long)count];
    NSURL *url = [self urlForEndpoint:@"getSimilarSongs2.view" params:params];
    NSDictionary *root = [self fetchJSON:url error:error];
    if (!root) return nil;

    NSMutableArray<SubsonicSong *> *result = [NSMutableArray array];
    for (NSDictionary *s in asArray(root[@"similarSongs2"][@"song"]))
        [result addObject:parseSong(s)];
    return result;
}

- (NSArray<SubsonicSong *> *)getRandomSongsWithCount:(NSInteger)count
                                                error:(NSError **)error {
    NSString *params = [NSString stringWithFormat:@"size=%ld", (long)count];
    NSURL *url = [self urlForEndpoint:@"getRandomSongs.view" params:params];
    NSDictionary *root = [self fetchJSON:url error:error];
    if (!root) return nil;

    NSMutableArray<SubsonicSong *> *result = [NSMutableArray array];
    for (NSDictionary *s in asArray(root[@"randomSongs"][@"song"]))
        [result addObject:parseSong(s)];
    return result;
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
    return [self fetchJSON:url error:error] != nil;
}

- (SubsonicSong *)getSongWithId:(NSString *)songId error:(NSError **)error {
    if (songId.length == 0) return nil;
    NSString *params = [NSString stringWithFormat:@"id=%@", urlEncode(songId)];
    NSURL *url = [self urlForEndpoint:@"getSong.view" params:params];
    NSDictionary *root = [self fetchJSON:url error:error];
    if (!root) return nil;
    NSDictionary *s = root[@"song"];
    return [s isKindOfClass:[NSDictionary class]] ? parseSong(s) : nil;
}

- (BOOL)setRating:(NSInteger)rating forSongId:(NSString *)songId error:(NSError **)error {
    if (songId.length == 0) return NO;
    NSString *params = [NSString stringWithFormat:@"id=%@&rating=%ld",
                        urlEncode(songId), (long)MAX(0, MIN(5, rating))];
    NSURL *url = [self urlForEndpoint:@"setRating.view" params:params];
    return [self fetchJSON:url error:error] != nil;
}

- (NSArray<SubsonicPlaylist *> *)getPlaylistsWithError:(NSError **)error {
    NSURL *url = [self urlForEndpoint:@"getPlaylists.view" params:@""];
    NSDictionary *root = [self fetchJSON:url error:error];
    if (!root) return nil;

    NSMutableArray<SubsonicPlaylist *> *result = [NSMutableArray array];
    for (NSDictionary *p in asArray(root[@"playlists"][@"playlist"])) {
        SubsonicPlaylist *pl = [[SubsonicPlaylist alloc] init];
        pl.playlistId = p[@"id"] ?: @"";
        pl.name       = p[@"name"] ?: @"Unnamed playlist";
        pl.owner      = p[@"owner"] ?: @"";
        pl.songCount  = [p[@"songCount"] integerValue];
        pl.duration   = [p[@"duration"] doubleValue];
        [result addObject:pl];
    }
    return result;
}

- (NSArray<SubsonicSong *> *)getPlaylistSongs:(NSString *)playlistId error:(NSError **)error {
    NSString *params = [NSString stringWithFormat:@"id=%@", urlEncode(playlistId)];
    NSURL *url = [self urlForEndpoint:@"getPlaylist.view" params:params];
    NSDictionary *root = [self fetchJSON:url error:error];
    if (!root) return nil;

    NSMutableArray<SubsonicSong *> *result = [NSMutableArray array];
    for (NSDictionary *s in asArray(root[@"playlist"][@"entry"]))
        [result addObject:parseSong(s)];
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

    NSDictionary *root = [self fetchJSON:[self urlForEndpoint:@"createPlaylist.view"
                                                       params:params]
                                   error:error];
    if (!root) return nil;

    // Navidrome echoes the created playlist back; without its id the remaining
    // tracks can't be appended (and the caller can't act on the new playlist).
    NSString *playlistId = root[@"playlist"][@"id"];
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

    for (NSUInteger i = 0; i < songIds.count; i += kChunk) {
        NSMutableString *upd = [NSMutableString stringWithFormat:@"playlistId=%@",
                                urlEncode(playlistId)];
        for (NSUInteger j = i; j < MIN(i + kChunk, songIds.count); j++)
            [upd appendFormat:@"&songIdToAdd=%@", urlEncode(songIds[j])];
        if (![self fetchJSON:[self urlForEndpoint:@"updatePlaylist.view" params:upd]
                       error:error])
            return NO;
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

    for (NSUInteger i = 0; i < sorted.count; i += kChunk) {
        NSMutableString *upd = [NSMutableString stringWithFormat:@"playlistId=%@",
                                urlEncode(playlistId)];
        for (NSUInteger j = i; j < MIN(i + kChunk, sorted.count); j++)
            [upd appendFormat:@"&songIndexToRemove=%ld", (long)sorted[j].integerValue];
        if (![self fetchJSON:[self urlForEndpoint:@"updatePlaylist.view" params:upd]
                       error:error])
            return NO;
    }
    return YES;
}

- (BOOL)renamePlaylist:(NSString *)playlistId
                toName:(NSString *)name
                 error:(NSError **)error {
    if (playlistId.length == 0 || name.length == 0) return NO;
    NSString *params = [NSString stringWithFormat:@"playlistId=%@&name=%@",
                        urlEncode(playlistId), urlEncode(name)];
    return [self fetchJSON:[self urlForEndpoint:@"updatePlaylist.view" params:params]
                     error:error] != nil;
}

- (BOOL)deletePlaylist:(NSString *)playlistId error:(NSError **)error {
    if (playlistId.length == 0) return NO;
    NSString *params = [NSString stringWithFormat:@"id=%@", urlEncode(playlistId)];
    return [self fetchJSON:[self urlForEndpoint:@"deletePlaylist.view" params:params]
                     error:error] != nil;
}

- (NSArray<SubsonicRadioStation *> *)getRadioStationsWithError:(NSError **)error {
    NSURL *url = [self urlForEndpoint:@"getInternetRadioStations.view" params:@""];
    NSDictionary *root = [self fetchJSON:url error:error];
    if (!root) return nil;

    NSMutableArray<SubsonicRadioStation *> *result = [NSMutableArray array];
    for (NSDictionary *s in asArray(root[@"internetRadioStations"][@"internetRadioStation"])) {
        SubsonicRadioStation *station = [[SubsonicRadioStation alloc] init];
        station.stationId   = s[@"id"] ?: @"";
        station.name        = s[@"name"] ?: @"Unnamed station";
        station.streamUrl   = s[@"streamUrl"] ?: @"";
        station.homePageUrl = s[@"homePageUrl"] ?: @"";
        [result addObject:station];
    }
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

    NSDictionary *root = [self fetchJSON:[self urlForEndpoint:@"createInternetRadioStation.view"
                                                       params:params]
                                   error:error];
    if (!root) return nil;
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
    return [self fetchJSON:[self urlForEndpoint:@"updateInternetRadioStation.view" params:params]
                     error:error] != nil;
}

- (BOOL)deleteRadioStation:(NSString *)stationId error:(NSError **)error {
    if (stationId.length == 0) return NO;
    NSString *params = [NSString stringWithFormat:@"id=%@", urlEncode(stationId)];
    return [self fetchJSON:[self urlForEndpoint:@"deleteInternetRadioStation.view" params:params]
                     error:error] != nil;
}

- (NSArray<SubsonicBookmark *> *)getBookmarksWithError:(NSError **)error {
    NSURL *url = [self urlForEndpoint:@"getBookmarks.view" params:@""];
    NSDictionary *root = [self fetchJSON:url error:error];
    if (!root) return nil;

    NSMutableArray<SubsonicBookmark *> *result = [NSMutableArray array];
    for (NSDictionary *b in asArray(root[@"bookmarks"][@"bookmark"])) {
        NSArray *entries = asArray(b[@"entry"]);
        if (entries.count == 0) continue;
        SubsonicBookmark *bm = [[SubsonicBookmark alloc] init];
        bm.song       = parseSong(entries[0]);
        bm.positionMs = [b[@"position"] doubleValue];
        bm.comment    = b[@"comment"] ?: @"";
        [result addObject:bm];
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
    return [self fetchJSON:[self urlForEndpoint:@"createBookmark.view" params:params]
                     error:error] != nil;
}

- (BOOL)deleteBookmarkForSongId:(NSString *)songId error:(NSError **)error {
    if (songId.length == 0) return NO;
    NSString *params = [NSString stringWithFormat:@"id=%@", urlEncode(songId)];
    return [self fetchJSON:[self urlForEndpoint:@"deleteBookmark.view" params:params]
                     error:error] != nil;
}

- (BOOL)fetchScanStatusForEndpoint:(NSString *)endpoint
                           scanning:(BOOL *)scanning
                              count:(NSInteger *)count
                              error:(NSError **)error {
    if (scanning) *scanning = NO;
    if (count)    *count    = 0;
    NSURL *url = [self urlForEndpoint:endpoint params:@""];
    NSDictionary *root = [self fetchJSON:url error:error];
    if (!root) return NO;
    NSDictionary *status = root[@"scanStatus"];
    if ([status isKindOfClass:[NSDictionary class]]) {
        if (scanning) *scanning = [status[@"scanning"] boolValue];
        if (count)    *count    = [status[@"count"] integerValue];
    }
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
    return [self fetchJSON:url error:error] != nil;
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
    NSMutableURLRequest *request = [NSMutableURLRequest requestWithURL:url];
    NavidromeApplyCustomHeaders(request);

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

    if (taskError) { if (outError) *outError = taskError; return nil; }
    if (httpResponse && httpResponse.statusCode != 200) {
        if (outError) *outError = [NSError errorWithDomain:@"SubsonicClient"
            code:httpResponse.statusCode userInfo:@{NSLocalizedDescriptionKey:
                [NSString stringWithFormat:@"HTTP %ld", (long)httpResponse.statusCode]}];
        return nil;
    }
    return responseData;
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
