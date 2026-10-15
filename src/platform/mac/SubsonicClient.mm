#import "SubsonicClient.h"
#import "../../core/SubsonicTypes.h"
#import "../../core/SubsonicCore.h"
#import "../../core/NavidromeDebugLog.h"
#import "../../core/NavidromeAudioMuse.h"
#include "../../core/NavidromeSettings.h"
#include "../../core/NavidromeDiagnostics.h"

#import <memory>

static void NavidromeApplyCustomHeaders(NSMutableURLRequest *req) {
    for (const std::string &line :
         navidrome::parseHeaderLines(navidrome::cfg_custom_headers.get().c_str())) {
        std::string name, value;
        if (!navidrome::splitHeaderLine(line, name, value)) continue;
        [req setValue:[NSString stringWithUTF8String:value.c_str()]
            forHTTPHeaderField:[NSString stringWithUTF8String:name.c_str()]];
    }
}

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


struct NavidromeSyncResponse {
    NSData *data = nil;
    NSHTTPURLResponse *response = nil;
    NSError *error = nil;
};

static NavidromeSyncResponse NavidromeSendSync(NSURLSession *session, NSURLRequest *request) {
    __block NavidromeSyncResponse out;
    dispatch_semaphore_t sema = dispatch_semaphore_create(0);
    [[session dataTaskWithRequest:request
                completionHandler:^(NSData *d, NSURLResponse *response, NSError *error) {
        out.data = d;
        out.error = error;
        out.response = (NSHTTPURLResponse *)response;
        dispatch_semaphore_signal(sema);
    }] resume];
    dispatch_semaphore_wait(sema, DISPATCH_TIME_FOREVER);
    return out;
}

static NSString *nsstr(const std::string &s) {
    return [NSString stringWithUTF8String:s.c_str()] ?: @"";
}

@implementation SubsonicPlaylist
- (NSString *)description {
    return [NSString stringWithFormat:@"<SubsonicPlaylist %@ %@>", _playlistId, _name];
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

static SubsonicPlaylist *PlaylistFromCore(const navidrome::Playlist &p) {
    SubsonicPlaylist *pl = [[SubsonicPlaylist alloc] init];
    pl.playlistId = nsstr(p.id);
    pl.name       = nsstr(p.name);
    pl.owner      = nsstr(p.owner);
    pl.songCount  = p.songCount;
    pl.duration   = p.duration;
    return pl;
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

static NSArray<SubsonicMusicFolder *> *MusicFoldersFromCore(const std::vector<navidrome::MusicFolder> &v) {
    NSMutableArray<SubsonicMusicFolder *> *a = [NSMutableArray arrayWithCapacity:v.size()];
    for (const auto &x : v) {
        SubsonicMusicFolder *mf = MusicFolderFromCore(x);
        if (mf.folderId.length > 0) [a addObject:mf];
    }
    return a;
}
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

namespace {

struct MacHttpTransport : navidrome::IHttpTransport {
    NSURLSession *session = nil;

    navidrome::HttpResult getOnce(const std::string &url) override {
        @autoreleasepool {
            return fetch(url);
        }
    }

    navidrome::HttpResult fetch(const std::string &url) {
        navidrome::HttpResult out;
        NSURL *nsurl = [NSURL URLWithString:nsstr(url)];
        if (!nsurl) {
            out.error = { navidrome::ErrorKind::Parse, 0, 0, "invalid URL" };
            return out;
        }
        NSMutableURLRequest *request = [NSMutableURLRequest requestWithURL:nsurl];
        NavidromeApplyCustomHeaders(request);

        const NavidromeSyncResponse r = NavidromeSendSync(session, request);
        NSData *data = r.data;
        NSError *taskError = r.error;
        NSHTTPURLResponse *httpResponse = r.response;

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

    void onAuthRejected() override { navidrome::warnAuthRejectedOnce(); }
};

struct MacSettingsProvider : navidrome::ISettingsProvider {
    navidrome::SubsonicSettings load() const override {
        navidrome::SubsonicSettings s;
        s.serverUrl     = navidrome::cfg_server_url.get().c_str();
        s.username      = navidrome::cfg_username.get().c_str();
        s.password      = navidrome::cfg_password.get().c_str();
        pfc::string8 salt = navidrome::cfg_salt.get();
        s.salt          = navidrome::effectiveSalt(salt.c_str());
        s.streamFormat  = navidrome::cfg_stream_format.get().c_str();
        s.maxBitrate    = static_cast<int>(navidrome::cfg_max_bitrate.get());
        s.libraryFilter = navidrome::cfg_library_filter.get();
        s.libraryIdsCsv = navidrome::cfg_library_ids.get().c_str();
        return s;
    }
};
}

@interface SubsonicClient () {
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

- (navidrome::SubsonicCore &)core {
    return *_core;
}

- (BOOL)pingWithError:(NSError **)error {
    std::string err;
    BOOL ok = _core->ping(err);
    if (!ok && error) *error = NavidromeMakeError(err, -2);
    return ok;
}

- (NSArray<SubsonicMusicFolder *> *)getMusicFoldersWithError:(NSError **)error {
    std::string err;
    auto folders = _core->getMusicFolders(err);
    if (!err.empty()) { if (error) *error = NavidromeMakeError(err, -2); return nil; }
    return MusicFoldersFromCore(folders);
}

- (void)refreshMusicFolders {
    _core->refreshMusicFolders();
}

- (NSArray<SubsonicPlaylist *> *)getPlaylistsWithError:(NSError **)error {
    std::string err;
    auto v = _core->getPlaylists(err);
    if (!err.empty()) { if (error) *error = NavidromeMakeError(err, -2); return nil; }
    NSMutableArray<SubsonicPlaylist *> *result = [NSMutableArray arrayWithCapacity:v.size()];
    for (const auto &p : v) [result addObject:PlaylistFromCore(p)];
    return result;
}

- (NSString *)createPlaylistNamed:(NSString *)name
                          songIds:(NSArray<NSString *> *)songIds
                            error:(NSError **)error {
    if (name.length == 0) return nil;
    std::string err;
    std::string pid = _core->createPlaylist(name.UTF8String ?: "", ToStdStrings(songIds), err);
    if (!err.empty()) { if (error) *error = NavidromeMakeError(err, -3); return nil; }
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

- (NSString *)createPodcastChannelWithURL:(NSString *)url error:(NSError **)error {
    if (url.length == 0) return nil;
    std::string err;
    _core->createPodcastChannel(url.UTF8String ?: "", err);
    if (!err.empty()) { if (error) *error = NavidromeMakeError(err, -2); return nil; }
    return @"";
}

- (BOOL)deletePodcastChannel:(NSString *)channelId error:(NSError **)error {
    std::string err;
    BOOL ok = _core->deletePodcastChannel(channelId.UTF8String ?: "", err);
    if (!ok && error) *error = NavidromeMakeError(err, -2);
    return ok;
}

- (BOOL)deleteBookmarkForSongId:(NSString *)songId error:(NSError **)error {
    std::string err;
    BOOL ok = _core->deleteBookmark(songId.UTF8String ?: "", err);
    if (!ok && error) *error = NavidromeMakeError(err, -2);
    return ok;
}

- (NSURL *)downloadURLForSongId:(NSString *)songId {
    return [NSURL URLWithString:nsstr(_core->downloadURL(songId.UTF8String ?: ""))];
}

- (NSURL *)coverArtURLForId:(NSString *)coverArtId size:(NSInteger)size {
    return [NSURL URLWithString:nsstr(_core->coverArtURL(coverArtId.UTF8String ?: "", (int)size))];
}

- (NSData *)dataForURL:(NSURL *)url error:(NSError **)outError {
    const std::string safeUrl =
        navidrome::dbg::scrubAuth(std::string(url.absoluteString.UTF8String ?: ""));
    NAVIDROME_TIMER("Art", "GET " + safeUrl);

    NSMutableURLRequest *request = [NSMutableURLRequest requestWithURL:url];
    NavidromeApplyCustomHeaders(request);

    for (int attempt = 1; attempt <= navidrome::retry::kMaxAttempts; ++attempt) {
        const NavidromeSyncResponse r = NavidromeSendSync(_session, request);
        NSData *responseData = r.data;
        NSError *taskError = r.error;
        NSHTTPURLResponse *httpResponse = r.response;

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
        if (!navidrome::retry::again(err, attempt)) {
            NAVIDROME_WARN("Art", std::string(err.kindName()) + ": " + err.message +
                           "  (" + safeUrl + ")");
            if (outError) {
                *outError = taskError ?: [NSError errorWithDomain:@"SubsonicClient"
                    code:err.http userInfo:@{NSLocalizedDescriptionKey:
                        [NSString stringWithUTF8String:err.message.c_str()]}];
            }
            return nil;
        }
        _transport->sleepMs(navidrome::retry::backoffMs(attempt, _transport->jitterMs()));
    }
    return nil;
}

- (BOOL)downloadURL:(NSURL *)url toPath:(NSString *)path error:(NSError **)outError {
    NSMutableURLRequest *request = [NSMutableURLRequest requestWithURL:url];
    NavidromeApplyCustomHeaders(request);
    request.timeoutInterval = 300.0;

    __block NSURL *tempURL = nil;
    __block NSError *taskError = nil;
    __block NSHTTPURLResponse *httpResponse = nil;
    dispatch_semaphore_t sema = dispatch_semaphore_create(0);
    [[_session downloadTaskWithRequest:request
                     completionHandler:^(NSURL *location, NSURLResponse *response, NSError *error) {
        taskError    = error;
        httpResponse = (NSHTTPURLResponse *)response;
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

namespace {

struct MacJsonPoster : navidrome::audiomuse::IJsonPoster {
    navidrome::HttpResult postJson(const std::string &url, const std::string &body,
                                   const std::string &bearerToken, int timeoutMs) override {
        navidrome::HttpResult out;
        @autoreleasepool {
            NSURL *nsurl = [NSURL URLWithString:@(url.c_str())];
            if (!nsurl) {
                out.error = { navidrome::ErrorKind::Parse, 0, 0, "invalid AudioMuse-AI URL" };
                return out;
            }
            static NSURLSession *session = [] {
                NSURLSessionConfiguration *config = [NSURLSessionConfiguration ephemeralSessionConfiguration];
                config.timeoutIntervalForResource = navidrome::audiomuse::kPlaylistTimeoutMs / 1000.0;
                return [NSURLSession sessionWithConfiguration:config];
            }();
            NSMutableURLRequest *request = [NSMutableURLRequest requestWithURL:nsurl];
            request.HTTPMethod = @"POST";
            request.timeoutInterval = timeoutMs / 1000.0;
            request.HTTPBody = [NSData dataWithBytes:body.data() length:body.size()];
            [request setValue:@"application/json" forHTTPHeaderField:@"Content-Type"];
            [request setValue:@"application/json" forHTTPHeaderField:@"Accept"];
            if (!bearerToken.empty())
                [request setValue:[@"Bearer " stringByAppendingString:@(bearerToken.c_str())]
                    forHTTPHeaderField:@"Authorization"];

            const NavidromeSyncResponse r = NavidromeSendSync(session, request);
            NSData *data = r.data;
            NSError *taskError = r.error;
            NSHTTPURLResponse *httpResponse = r.response;

            if (taskError) {
                navidrome::ErrorKind kind = NavidromeClassifyURLError(taskError.code);
                out.error = { kind, 0, 0, std::string(navidrome::errorKindName(kind)) + ": " +
                              (taskError.localizedDescription.UTF8String ?: "?") };
                return out;
            }
            if (data) out.body.assign(static_cast<const char *>(data.bytes), data.length);
            const int status = (int)httpResponse.statusCode;
            const navidrome::ErrorKind kind = navidrome::httpStatusToErrorKind(status);
            out.error = { kind, status, 0,
                          kind == navidrome::ErrorKind::None ? std::string() : "HTTP " + std::to_string(status) };
        }
        return out;
    }
};
}

navidrome::audiomuse::IJsonPoster &navidrome::audioMusePoster() {
    static MacJsonPoster inst;
    return inst;
}
