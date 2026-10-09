#import "../../core/stdafx.h"
#import "SubsonicClient.h"
#import "NavidromeBrowserController.h"
#import "NavidromeLyricsController.h"
#import "NavidromePreferencesController.h"
#include <helpers/advconfig_impl.h>
#include <SDK/cfg_var.h>
#include <SDK/library_manager.h>
#include <SDK/play_callback.h>
#include <SDK/initquit.h>
#include <initializer_list>
#include <strings.h>
#include <SDK/ui_element_mac.h>
#include "../../core/SubsonicTypes.h"
#include "../../core/NavidromePlaylistSync.h"
#include "../../core/NavidromeDebugLog.h"
#include <algorithm>
#include <cstring>

static constexpr GUID guid_cfg_server_url  = { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01, 0x01 } };
static constexpr GUID guid_cfg_username    = { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01, 0x02 } };
static constexpr GUID guid_cfg_password    = { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01, 0x03 } };
static constexpr GUID guid_cfg_salt        = { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01, 0x04 } };
static constexpr GUID guid_prefs_page      = { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01, 0x05 } };
static constexpr GUID guid_mainmenu_cmd    = { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01, 0x07 } };
static constexpr GUID guid_library_viewer  = { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01, 0x08 } };
static constexpr GUID guid_library_prefs   = { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01, 0x09 } };
static constexpr GUID guid_cfg_custom_headers = { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01, 0x0a } };
static constexpr GUID guid_cfg_scrobble    = { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01, 0x0b } };
static constexpr GUID guid_cfg_stream_format = { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01, 0x0c } };
static constexpr GUID guid_cfg_max_bitrate = { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01, 0x0d } };
static constexpr GUID guid_ui_element_mac  = { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01, 0x0e } };
static constexpr GUID guid_radio_prefs_page = { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01, 0x0f } };
static constexpr GUID guid_cfg_library_filter = { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01, 0x10 } };
static constexpr GUID guid_cfg_library_ids  = { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01, 0x11 } };
static constexpr GUID guid_libsel_prefs_page = { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01, 0x12 } };
static constexpr GUID guid_ui_element_mac_lyrics = { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01, 0x13 } };
static constexpr GUID guid_audiomuse_prefs_page = { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x04, 0x05 } };

namespace navidrome {

    cfg_string cfg_server_url(guid_cfg_server_url, "http://navidrome.santirod.local:4533/");
    cfg_string cfg_username  (guid_cfg_username,   "");
    cfg_string cfg_password  (guid_cfg_password,   "");
    cfg_string cfg_salt      (guid_cfg_salt,        "fb2k_navidrome");
    cfg_string cfg_custom_headers(guid_cfg_custom_headers, "");
    cfg_var_modern::cfg_bool cfg_scrobble(guid_cfg_scrobble, true);

    cfg_string cfg_stream_format(guid_cfg_stream_format, "");
    cfg_var_modern::cfg_int cfg_max_bitrate(guid_cfg_max_bitrate, 0);

    cfg_var_modern::cfg_bool cfg_library_filter(guid_cfg_library_filter, false);
    cfg_string cfg_library_ids(guid_cfg_library_ids, "");
}

namespace {

class navidrome_scrobbler : public play_callback_static {
public:
    unsigned get_flags() override {
        return flag_on_playback_new_track | flag_on_playback_time |
               flag_on_playback_stop;
    }

    void on_playback_new_track(metadb_handle_ptr track) override {
        auto a = m_tracker.onNewTrack(
            track.is_empty() ? std::string() : std::string(track->get_path()),
            track.is_empty() ? 0.0 : track->get_length(),
            navidrome::cfg_scrobble.get());
        if (!a.refreshRatingId.empty()) refreshRatingAsync(a.refreshRatingId);
        if (!a.scrobbleNowId.empty())   scrobbleAsync(a.scrobbleNowId, NO);
    }

    void on_playback_time(double time) override {
        std::string id = m_tracker.onPlaybackTime(time);
        if (!id.empty()) scrobbleAsync(id, YES);
    }

    void on_playback_stop(play_control::t_stop_reason) override {
        m_tracker.onStop();
    }

    void on_playback_starting(play_control::t_track_command, bool) override {}
    void on_playback_seek(double) override {}
    void on_playback_pause(bool) override {}
    void on_playback_edited(metadb_handle_ptr) override {}
    void on_playback_dynamic_info(const file_info &) override {}
    void on_playback_dynamic_info_track(const file_info &) override {}
    void on_volume_change(float) override {}

private:
    static void scrobbleAsync(std::string songId, BOOL submission) {
        dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
            navidrome::dbg::runGuarded("Scrobble", "scrobbleAsync", [&]{
                NSError *err = nil;
                [SubsonicClient.sharedClient
                    scrobbleSongId:[NSString stringWithUTF8String:songId.c_str()]
                        submission:submission
                             error:&err];
                navidrome::Error e = [SubsonicClient.sharedClient lastError];
                NAVIDROME_LOG("Scrobble", std::string(submission ? "submit" : "now-playing") +
                              " id=" + songId + (e.ok() ? " ok"
                              : std::string(" FAILED ") + e.kindName() + ": " + e.message));
            });
        });
    }

    static void refreshRatingAsync(std::string songId) {
        dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
            navidrome::dbg::runGuarded("Rating", "refreshRatingAsync", [&]{
                NSError *err = nil;
                SubsonicSong *song = [SubsonicClient.sharedClient
                    getSongWithId:[NSString stringWithUTF8String:songId.c_str()]
                            error:&err];
                if (!song) {
                    NAVIDROME_WARN("Rating", "getSong id=" + songId + " failed: " +
                                   [SubsonicClient.sharedClient lastError].kindName());
                    return;
                }
                navidrome::RatingUpdate u;
                u.songId  = songId;
                u.rating  = (int)song.rating;
                u.starred = song.starred ? true : false;
                std::vector<navidrome::RatingUpdate> updates;
                updates.push_back(std::move(u));
                navidrome::syncRatingsToPlaylists(std::move(updates));
                NAVIDROME_LOG("Rating", "id=" + songId + " -> rating=" +
                              std::to_string(u.rating) + " starred=" + (u.starred ? "1" : "0"));
            });
        });
    }

    navidrome::ScrobbleTracker m_tracker;
};

static play_callback_static_factory_t<navidrome_scrobbler> g_navidrome_scrobbler_factory;

static void navidromeLogSessionEnv() {
#ifdef NAVIDROME_DEBUG_LOG
    navidrome::SessionEnv e;
    e.platform        = "macOS";
    e.configured      = [SubsonicClient.sharedClient isConfigured];
    e.serverUrl       = navidrome::cfg_server_url.get().c_str();
    e.transcodeFormat = navidrome::cfg_stream_format.get().c_str();
    e.maxBitrate      = (int)navidrome::cfg_max_bitrate.get();
    e.scrobble        = navidrome::cfg_scrobble.get();
    e.startupRefresh  = navidrome::refreshRatingsOnStartEnabled();
    e.customHeaders   = navidrome::cfg_custom_headers.get().length() > 0;
    NAVIDROME_LOG("Env", navidrome::describeSessionEnv(e));
#endif
}

static void navidromeRefreshRatingsOnStart() {
    navidromeLogSessionEnv();

    navidrome::PlaylistAlbumScan scan = navidrome::scanPlaylistAlbums();

    if (scan.entries == 0) return;

    if (!navidrome::refreshRatingsOnStartEnabled()) {
        NAVIDROME_LOG("Rating", "startup refresh: disabled by advconfig switch");
        return;
    }
    if (![SubsonicClient.sharedClient isConfigured]) {
        NAVIDROME_LOG("Rating", "startup refresh: no server configured");
        return;
    }
    NAVIDROME_LOG("Rating", "startup refresh: " + std::to_string(scan.entries) +
                  " entries, " + std::to_string(scan.albumIds.size()) + " distinct albums, " +
                  std::to_string(scan.ungrouped) + " ungrouped");

    if (scan.albumIds.empty()) {
        pfc::string_formatter msg;
        msg << "Navidrome: " << scan.entries << " playlist entry/entries carry no "
               "album id (added by an older version) — open their album in the "
               "browser to refresh them";
        console::print(msg.c_str());
        return;
    }

    auto albumIds = std::move(scan.albumIds);
    const size_t ungrouped = scan.ungrouped;
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_BACKGROUND, 0), ^{
        @autoreleasepool {
          navidrome::dbg::runGuarded("Rating", "startup refresh worker", [&]{
            std::vector<navidrome::RatingUpdate> updates;
            size_t failed = 0;
            for (const std::string &albumId : albumIds) {
                NSError *err = nil;
                NSArray<SubsonicSong *> *songs = [SubsonicClient.sharedClient
                    getSongsForAlbum:[NSString stringWithUTF8String:albumId.c_str()]
                               error:&err];
                if (err) { failed++; continue; }
                for (SubsonicSong *song in songs) {
                    if (song.songId.length == 0) continue;
                    navidrome::RatingUpdate u;
                    u.songId  = [song.songId UTF8String];
                    u.rating  = (int)song.rating;
                    u.starred = song.starred ? true : false;
                    updates.push_back(std::move(u));
                }
            }
            navidrome::syncRatingsToPlaylists(std::move(updates));

            pfc::string_formatter msg;
            msg << "Navidrome: refreshed ratings from " << (albumIds.size() - failed)
                << " album(s)";
            if (failed > 0)    msg << ", " << failed << " album(s) failed";
            if (ungrouped > 0) msg << ", " << ungrouped
                                   << " entry/entries skipped (no album id, added by an "
                                      "older version)";
            console::print(msg.c_str());
            NAVIDROME_LOG("Rating", std::string("startup refresh done: ") + msg.c_str());
          });
        }
    });
}

class navidrome_startup_refresh : public initquit {
public:
    void on_init() override { navidromeRefreshRatingsOnStart(); }
};

static initquit_factory_t<navidrome_startup_refresh> g_navidrome_startup_refresh_factory;
}
bool navidrome::setRatingOnServer(const std::string &songId, int rating) {
    @autoreleasepool {
        NSError *err = nil;
        return [SubsonicClient.sharedClient
            setRating:rating
            forSongId:[NSString stringWithUTF8String:songId.c_str()]
                error:&err] ? true : false;
    }
}

bool navidrome::setStarredOnServer(const std::string &songId, bool starred) {
    @autoreleasepool {
        NSError *err = nil;
        return [SubsonicClient.sharedClient
            setStarred:starred ? YES : NO
                 forId:[NSString stringWithUTF8String:songId.c_str()]
                  kind:SubsonicStarKindSong
                 error:&err] ? true : false;
    }
}

namespace {

class preferences_page_navidrome : public preferences_page {
public:
    service_ptr instantiate() override {
        return fb2k::wrapNSObject([NavidromePreferencesController new]);
    }
    const char *get_name() override { return "Navidrome"; }
    GUID get_guid() override { return guid_prefs_page; }
    GUID get_parent_guid() override { return guid_tools; }
};

FB2K_SERVICE_FACTORY(preferences_page_navidrome);
}

@interface NavidromeRadioPrefsController : NSViewController <NSTableViewDataSource, NSTableViewDelegate>
@end

@implementation NavidromeRadioPrefsController {
    NSTableView *_tableView;
    NSTextField *_statusLabel;
    NSButton *_newButton, *_editButton, *_deleteButton;
    NSArray<SubsonicRadioStation *> *_stations;
}

- (instancetype)init {
    self = [super initWithNibName:nil bundle:nil];
    return self;
}

- (void)loadView {
    NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 480, 320)];

    NSScrollView *scroll = [[NSScrollView alloc] init];
    scroll.translatesAutoresizingMaskIntoConstraints = NO;
    scroll.hasVerticalScroller = YES;
    scroll.borderType = NSBezelBorder;

    _tableView = [[NSTableView alloc] init];
    _tableView.dataSource = self;
    _tableView.delegate = self;
    _tableView.usesAlternatingRowBackgroundColors = YES;

    NSTableColumn *nameCol = [[NSTableColumn alloc] initWithIdentifier:@"name"];
    nameCol.title = @"Name";
    nameCol.width = 140;
    [_tableView addTableColumn:nameCol];

    NSTableColumn *urlCol = [[NSTableColumn alloc] initWithIdentifier:@"streamUrl"];
    urlCol.title = @"Stream URL";
    urlCol.width = 220;
    [_tableView addTableColumn:urlCol];

    NSTableColumn *homeCol = [[NSTableColumn alloc] initWithIdentifier:@"homePageUrl"];
    homeCol.title = @"Home Page";
    homeCol.width = 140;
    [_tableView addTableColumn:homeCol];

    scroll.documentView = _tableView;
    [root addSubview:scroll];

    _newButton = [NSButton buttonWithTitle:@"New…" target:self action:@selector(newStation:)];
    _newButton.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:_newButton];

    _editButton = [NSButton buttonWithTitle:@"Edit…" target:self action:@selector(editStation:)];
    _editButton.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:_editButton];

    _deleteButton = [NSButton buttonWithTitle:@"Delete…" target:self action:@selector(deleteStation:)];
    _deleteButton.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:_deleteButton];

    _statusLabel = [NSTextField labelWithString:@""];
    _statusLabel.translatesAutoresizingMaskIntoConstraints = NO;
    _statusLabel.textColor = [NSColor secondaryLabelColor];
    _statusLabel.font = [NSFont systemFontOfSize:11];
    [root addSubview:_statusLabel];

    CGFloat pad = 16, btnGap = 8, btnH = 24;

    [NSLayoutConstraint activateConstraints:@[
        [scroll.topAnchor constraintEqualToAnchor:root.topAnchor constant:pad],
        [scroll.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:pad],
        [scroll.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-pad],
        [scroll.bottomAnchor constraintEqualToAnchor:_newButton.topAnchor constant:-pad],

        [_newButton.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:pad],
        [_newButton.bottomAnchor constraintEqualToAnchor:root.bottomAnchor constant:-pad],
        [_newButton.heightAnchor constraintEqualToConstant:btnH],

        [_editButton.leadingAnchor constraintEqualToAnchor:_newButton.trailingAnchor constant:btnGap],
        [_editButton.centerYAnchor constraintEqualToAnchor:_newButton.centerYAnchor],

        [_deleteButton.leadingAnchor constraintEqualToAnchor:_editButton.trailingAnchor constant:btnGap],
        [_deleteButton.centerYAnchor constraintEqualToAnchor:_newButton.centerYAnchor],

        [_statusLabel.leadingAnchor constraintEqualToAnchor:_deleteButton.trailingAnchor constant:pad],
        [_statusLabel.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-pad],
        [_statusLabel.centerYAnchor constraintEqualToAnchor:_newButton.centerYAnchor],
    ]];

    self.view = root;
}

- (void)viewDidLoad {
    [super viewDidLoad];
    [self refresh];
}

- (void)refresh {
    if (!SubsonicClient.sharedClient.isConfigured) {
        _statusLabel.stringValue = @"Not configured";
        return;
    }
    _statusLabel.stringValue = @"Loading…";
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        NSError *err = nil;
        NSArray<SubsonicRadioStation *> *stations =
            [SubsonicClient.sharedClient getRadioStationsWithError:&err];
        dispatch_async(dispatch_get_main_queue(), ^{
            if (err || !stations) {
                self->_statusLabel.stringValue = [NSString stringWithFormat:@"Failed: %@",
                    err.localizedDescription ?: @"unknown error"];
                return;
            }
            self->_stations = stations;
            [self->_tableView reloadData];
            self->_statusLabel.stringValue = stations.count == 0 ? @"No radio stations" : @"";
        });
    });
}

#pragma mark - NSTableViewDataSource / Delegate

- (NSInteger)numberOfRowsInTableView:(NSTableView *)tableView { return _stations.count; }

- (NSView *)tableView:(NSTableView *)tableView viewForTableColumn:(NSTableColumn *)tableColumn row:(NSInteger)row {
    SubsonicRadioStation *s = _stations[row];
    NSString *text = [tableColumn.identifier isEqualToString:@"name"] ? s.name
                    : [tableColumn.identifier isEqualToString:@"streamUrl"] ? s.streamUrl
                    : (s.homePageUrl ?: @"");
    NSTextField *field = [NSTextField labelWithString:text ?: @""];
    field.lineBreakMode = NSLineBreakByTruncatingTail;
    return field;
}

#pragma mark - Actions

- (IBAction)newStation:(id)sender {
    NSString *name = nil, *streamUrl = nil, *homePageUrl = nil;
    if (![self promptForRadioStationWithTitle:@"New Radio Station"
                                          name:&name streamURL:&streamUrl homePageURL:&homePageUrl])
        return;
    if (name.length == 0 || streamUrl.length == 0) {
        _statusLabel.stringValue = @"Name and stream URL are required";
        return;
    }
    _statusLabel.stringValue = @"Creating…";
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        NSError *err = nil;
        NSString *result = [SubsonicClient.sharedClient createRadioStationWithStreamURL:streamUrl
                                                                                    name:name
                                                                             homePageUrl:homePageUrl
                                                                                   error:&err];
        if (!result)
            NAVIDROME_WARN("UI", "create radio station \"" + std::string(name.UTF8String ?: "") +
                                 "\": " + std::string(err.localizedDescription.UTF8String ?: "unknown error"));
        dispatch_async(dispatch_get_main_queue(), ^{
            if (result) [self refresh];
            else self->_statusLabel.stringValue = [NSString stringWithFormat:@"Failed: %@",
                err.localizedDescription ?: @"unknown error"];
        });
    });
}

- (IBAction)editStation:(id)sender {
    NSInteger row = _tableView.selectedRow;
    if (row < 0 || row >= (NSInteger)_stations.count) {
        _statusLabel.stringValue = @"Select a station";
        return;
    }
    SubsonicRadioStation *current = _stations[row];
    NSString *name = nil, *streamUrl = nil, *homePageUrl = nil;
    if (![self promptForRadioStationWithTitle:@"Edit Radio Station"
                                   initialName:current.name ?: @""
                              initialStreamURL:current.streamUrl ?: @""
                            initialHomePageURL:current.homePageUrl ?: @""
                                          name:&name streamURL:&streamUrl homePageURL:&homePageUrl])
        return;
    if (name.length == 0 || streamUrl.length == 0) {
        _statusLabel.stringValue = @"Name and stream URL are required";
        return;
    }
    NSString *stationId = current.stationId;
    _statusLabel.stringValue = @"Updating…";
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        NSError *err = nil;
        BOOL ok = [SubsonicClient.sharedClient updateRadioStation:stationId
                                                          streamURL:streamUrl
                                                               name:name
                                                        homePageUrl:homePageUrl
                                                              error:&err];
        if (!ok)
            NAVIDROME_WARN("UI", "update radio station " + std::string(stationId.UTF8String ?: "") +
                                 ": " + std::string(err.localizedDescription.UTF8String ?: "unknown error"));
        dispatch_async(dispatch_get_main_queue(), ^{
            if (ok) [self refresh];
            else self->_statusLabel.stringValue = [NSString stringWithFormat:@"Failed: %@",
                err.localizedDescription ?: @"unknown error"];
        });
    });
}

- (IBAction)deleteStation:(id)sender {
    NSInteger row = _tableView.selectedRow;
    if (row < 0 || row >= (NSInteger)_stations.count) {
        _statusLabel.stringValue = @"Select a station";
        return;
    }
    SubsonicRadioStation *current = _stations[row];

    NSAlert *confirm = [[NSAlert alloc] init];
    confirm.messageText = [NSString stringWithFormat:@"Delete “%@” from the server?", current.name];
    confirm.informativeText = @"The station is removed for every client.";
    confirm.alertStyle = NSAlertStyleWarning;
    [confirm addButtonWithTitle:@"Delete"];
    [confirm addButtonWithTitle:@"Cancel"];
    if ([confirm runModal] != NSAlertFirstButtonReturn) return;

    NSString *stationId = current.stationId;
    _statusLabel.stringValue = @"Deleting…";
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        NSError *err = nil;
        BOOL ok = [SubsonicClient.sharedClient deleteRadioStation:stationId error:&err];
        if (!ok)
            NAVIDROME_WARN("UI", "delete radio station " + std::string(stationId.UTF8String ?: "") +
                                 ": " + std::string(err.localizedDescription.UTF8String ?: "unknown error"));
        dispatch_async(dispatch_get_main_queue(), ^{
            if (ok) [self refresh];
            else self->_statusLabel.stringValue = [NSString stringWithFormat:@"Failed: %@",
                err.localizedDescription ?: @"unknown error"];
        });
    });
}

- (BOOL)promptForRadioStationWithTitle:(NSString *)title
                                   name:(NSString **)outName
                              streamURL:(NSString **)outStreamURL
                            homePageURL:(NSString **)outHomePageURL {
    return [self promptForRadioStationWithTitle:title
                                     initialName:@""
                                initialStreamURL:@""
                              initialHomePageURL:@""
                                            name:outName
                                       streamURL:outStreamURL
                                     homePageURL:outHomePageURL];
}

- (BOOL)promptForRadioStationWithTitle:(NSString *)title
                            initialName:(NSString *)initialName
                       initialStreamURL:(NSString *)initialStreamURL
                     initialHomePageURL:(NSString *)initialHomePageURL
                                   name:(NSString **)outName
                              streamURL:(NSString **)outStreamURL
                            homePageURL:(NSString **)outHomePageURL {
    NSAlert *alert = [[NSAlert alloc] init];
    alert.messageText = title;
    alert.informativeText = @"Name and stream URL are required. Home page URL is optional.";
    [alert addButtonWithTitle:@"OK"];
    [alert addButtonWithTitle:@"Cancel"];

    CGFloat fieldWidth = 260, rowHeight = 24, rowGap = 6, labelHeight = 16;
    NSView *container = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, fieldWidth, 3 * (rowHeight + labelHeight + rowGap))];

    NSTextField *nameLabel = [NSTextField labelWithString:@"Name:"];
    NSTextField *nameField = [[NSTextField alloc] init];
    nameField.stringValue = initialName ?: @"";

    NSTextField *urlLabel = [NSTextField labelWithString:@"Stream URL:"];
    NSTextField *urlField = [[NSTextField alloc] init];
    urlField.stringValue = initialStreamURL ?: @"";

    NSTextField *homeLabel = [NSTextField labelWithString:@"Home page URL (optional):"];
    NSTextField *homeField = [[NSTextField alloc] init];
    homeField.stringValue = initialHomePageURL ?: @"";

    CGFloat y = 3 * (rowHeight + labelHeight + rowGap) - labelHeight;
    for (NSArray *pair in @[@[nameLabel, nameField], @[urlLabel, urlField], @[homeLabel, homeField]]) {
        NSTextField *label = pair[0];
        NSTextField *field = pair[1];
        label.frame = NSMakeRect(0, y, fieldWidth, labelHeight);
        [container addSubview:label];
        y -= (rowHeight + 2);
        field.frame = NSMakeRect(0, y, fieldWidth, rowHeight);
        [container addSubview:field];
        y -= rowGap;
    }

    alert.accessoryView = container;
    [alert layout];
    [alert.window setInitialFirstResponder:nameField];

    if ([alert runModal] != NSAlertFirstButtonReturn) return NO;

    [nameField validateEditing];
    [urlField validateEditing];
    [homeField validateEditing];

    NSCharacterSet *ws = [NSCharacterSet whitespaceAndNewlineCharacterSet];
    if (outName)        *outName        = [nameField.stringValue stringByTrimmingCharactersInSet:ws];
    if (outStreamURL)   *outStreamURL   = [urlField.stringValue stringByTrimmingCharactersInSet:ws];
    if (outHomePageURL) *outHomePageURL = [homeField.stringValue stringByTrimmingCharactersInSet:ws];
    return YES;
}

@end

namespace {

class preferences_page_navidrome_radio : public preferences_page {
public:
    service_ptr instantiate() override {
        return fb2k::wrapNSObject([NavidromeRadioPrefsController new]);
    }
    const char *get_name() override { return "Radio Stations"; }
    GUID get_guid() override { return guid_radio_prefs_page; }
    GUID get_parent_guid() override { return guid_prefs_page; }
};

FB2K_SERVICE_FACTORY(preferences_page_navidrome_radio);
}

@interface NavidromeLibrarySelectionPrefsController
    : NSViewController <NSTableViewDataSource, NSTableViewDelegate>
@end

@implementation NavidromeLibrarySelectionPrefsController {
    NSButton *_filterCheckbox;
    NSTableView *_tableView;
    NSTextField *_statusLabel;
    NSArray<SubsonicMusicFolder *> *_folders;
    NSMutableSet<NSString *> *_selectedIds;
    BOOL _rowsEnabled;
}

- (instancetype)init {
    self = [super initWithNibName:nil bundle:nil];
    if (self) {
        _folders = @[];
        _selectedIds = [NSMutableSet set];
        for (const auto &s :
             navidrome::parseMusicFolderIds(navidrome::cfg_library_ids.get().c_str()))
            [_selectedIds addObject:[NSString stringWithUTF8String:s.c_str()]];
    }
    return self;
}

- (void)loadView {
    NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 480, 320)];

    _filterCheckbox = [NSButton checkboxWithTitle:@"Only include selected libraries"
                                           target:self
                                           action:@selector(filterToggled:)];
    _filterCheckbox.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:_filterCheckbox];

    NSScrollView *scroll = [[NSScrollView alloc] init];
    scroll.translatesAutoresizingMaskIntoConstraints = NO;
    scroll.hasVerticalScroller = YES;
    scroll.borderType = NSBezelBorder;

    _tableView = [[NSTableView alloc] init];
    _tableView.dataSource = self;
    _tableView.delegate = self;
    _tableView.usesAlternatingRowBackgroundColors = YES;
    _tableView.headerView = nil;

    NSTableColumn *includeCol = [[NSTableColumn alloc] initWithIdentifier:@"include"];
    includeCol.width = 22;
    includeCol.resizingMask = NSTableColumnNoResizing;
    [_tableView addTableColumn:includeCol];

    NSTableColumn *nameCol = [[NSTableColumn alloc] initWithIdentifier:@"name"];
    nameCol.width = 400;
    [_tableView addTableColumn:nameCol];

    scroll.documentView = _tableView;
    [root addSubview:scroll];

    _statusLabel = [NSTextField labelWithString:@""];
    _statusLabel.translatesAutoresizingMaskIntoConstraints = NO;
    _statusLabel.textColor = [NSColor secondaryLabelColor];
    _statusLabel.font = [NSFont systemFontOfSize:11];
    [root addSubview:_statusLabel];

    CGFloat pad = 16;
    [NSLayoutConstraint activateConstraints:@[
        [_filterCheckbox.topAnchor constraintEqualToAnchor:root.topAnchor constant:pad],
        [_filterCheckbox.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:pad],
        [_filterCheckbox.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-pad],

        [scroll.topAnchor constraintEqualToAnchor:_filterCheckbox.bottomAnchor constant:pad],
        [scroll.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:pad],
        [scroll.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-pad],
        [scroll.bottomAnchor constraintEqualToAnchor:_statusLabel.topAnchor constant:-pad],

        [_statusLabel.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:pad],
        [_statusLabel.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-pad],
        [_statusLabel.bottomAnchor constraintEqualToAnchor:root.bottomAnchor constant:-pad],
    ]];

    self.view = root;
}

- (void)viewDidLoad {
    [super viewDidLoad];
    _filterCheckbox.state = navidrome::cfg_library_filter.get()
        ? NSControlStateValueOn : NSControlStateValueOff;
    [self refresh];
}

- (void)recomputeEnabled {
    BOOL multi = _folders.count >= 2;
    _filterCheckbox.enabled = multi;
    _rowsEnabled = multi && (_filterCheckbox.state == NSControlStateValueOn);
    [_tableView reloadData];
}

- (void)refresh {
    if (!SubsonicClient.sharedClient.isConfigured) {
        _statusLabel.stringValue = @"Not configured";
        return;
    }
    _statusLabel.stringValue = @"Loading…";
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        NSError *err = nil;
        NSArray<SubsonicMusicFolder *> *folders =
            [SubsonicClient.sharedClient getMusicFoldersWithError:&err];
        dispatch_async(dispatch_get_main_queue(), ^{
            if (err || !folders) {
                self->_statusLabel.stringValue = [NSString stringWithFormat:@"Failed: %@",
                    err.localizedDescription ?: @"unknown error"];
                return;
            }
            self->_folders = folders;
            self->_statusLabel.stringValue = folders.count < 2
                ? @"This server reports a single library — nothing to filter."
                : @"";
            [self recomputeEnabled];
        });
    });
}

#pragma mark - Actions

- (IBAction)filterToggled:(id)sender {
    BOOL on = _filterCheckbox.state == NSControlStateValueOn;
    navidrome::cfg_library_filter.set(on);
    if (!on) {
        [_selectedIds removeAllObjects];
        navidrome::cfg_library_ids.set("");
    }
    [SubsonicClient.sharedClient refreshMusicFolders];
    [self recomputeEnabled];
}

- (IBAction)rowToggled:(NSButton *)sender {
    NSInteger row = [_tableView rowForView:sender];
    if (row < 0 || row >= (NSInteger)_folders.count) return;
    NSString *fid = _folders[row].folderId;
    if (sender.state == NSControlStateValueOn) [_selectedIds addObject:fid];
    else                                       [_selectedIds removeObject:fid];

    std::vector<std::string> ids;
    for (SubsonicMusicFolder *f in _folders)
        if ([_selectedIds containsObject:f.folderId])
            ids.push_back(f.folderId.UTF8String ?: "");
    navidrome::cfg_library_ids.set(navidrome::joinMusicFolderIds(ids).c_str());
    [SubsonicClient.sharedClient refreshMusicFolders];
}

#pragma mark - NSTableViewDataSource / Delegate

- (NSInteger)numberOfRowsInTableView:(NSTableView *)tableView { return _folders.count; }

- (NSView *)tableView:(NSTableView *)tableView
   viewForTableColumn:(NSTableColumn *)tableColumn
                  row:(NSInteger)row {
    SubsonicMusicFolder *folder = _folders[row];
    if ([tableColumn.identifier isEqualToString:@"include"]) {
        NSButton *check = [NSButton checkboxWithTitle:@""
                                              target:self
                                              action:@selector(rowToggled:)];
        check.state = [_selectedIds containsObject:folder.folderId]
            ? NSControlStateValueOn : NSControlStateValueOff;
        check.enabled = _rowsEnabled;
        return check;
    }
    NSString *name = folder.name.length ? folder.name : folder.folderId;
    NSTextField *field = [NSTextField labelWithString:name ?: @""];
    field.lineBreakMode = NSLineBreakByTruncatingTail;
    return field;
}

@end

namespace {

class preferences_page_navidrome_libsel : public preferences_page {
public:
    service_ptr instantiate() override {
        return fb2k::wrapNSObject([NavidromeLibrarySelectionPrefsController new]);
    }
    const char *get_name() override { return "Libraries"; }
    GUID get_guid() override { return guid_libsel_prefs_page; }
    GUID get_parent_guid() override { return guid_prefs_page; }
};

FB2K_SERVICE_FACTORY(preferences_page_navidrome_libsel);

class preferences_page_navidrome_audiomuse : public preferences_page {
public:
    service_ptr instantiate() override {
        return fb2k::wrapNSObject([NavidromeAudioMusePrefsController new]);
    }
    const char *get_name() override { return "AudioMuse-AI"; }
    GUID get_guid() override { return guid_audiomuse_prefs_page; }
    GUID get_parent_guid() override { return guid_prefs_page; }
};

FB2K_SERVICE_FACTORY(preferences_page_navidrome_audiomuse);

class mainmenu_navidrome : public mainmenu_commands {
public:
    t_uint32 get_command_count() override { return 1; }

    GUID get_command(t_uint32 p_index) override {
        if (p_index == 0) return guid_mainmenu_cmd;
        throw pfc::exception_invalid_params();
    }

    void get_name(t_uint32 p_index, pfc::string_base &p_out) override {
        if (p_index == 0) { p_out = "Open Navidrome Browser"; return; }
        throw pfc::exception_invalid_params();
    }

    bool get_description(t_uint32 p_index, pfc::string_base &p_out) override {
        if (p_index == 0) {
            p_out = "Browse and stream music from your Navidrome server";
            return true;
        }
        return false;
    }

    GUID get_parent() override { return mainmenu_groups::file; }

    t_uint32 get_sort_priority() override { return 0xFF; }

    bool get_display(t_uint32 p_index, pfc::string_base &p_out, t_uint32 &p_flags) override {
        get_name(p_index, p_out);
        p_flags = 0;
        return true;
    }

    void execute(t_uint32 p_index, service_ptr_t<service_base> p_callback) override {
        if (p_index != 0) throw pfc::exception_invalid_params();
        NavidromeShowStandaloneBrowser();
    }
};

FB2K_SERVICE_FACTORY(mainmenu_navidrome);

class library_viewer_navidrome : public library_viewer {
public:
    GUID get_preferences_page() override { return guid_library_prefs; }
    bool have_activate()        override { return true; }

    void activate() override { NavidromeShowStandaloneBrowser(); }

    GUID         get_guid() override { return guid_library_viewer; }
    const char * get_name() override { return "Navidrome"; }
};

static library_viewer_factory_t<library_viewer_navidrome> g_library_viewer_navidrome_factory;
}

namespace {

class preferences_page_navidrome_library : public preferences_page {
public:
    service_ptr instantiate() override {
        return fb2k::wrapNSObject([NavidromeBrowserController new]);
    }
    const char *get_name() override { return "Navidrome"; }
    GUID get_guid() override { return guid_library_prefs; }
    GUID get_parent_guid() override { return guid_media_library; }
};

FB2K_SERVICE_FACTORY(preferences_page_navidrome_library);

static bool matchesLayoutName(const char *name, std::initializer_list<const char *> names) {
    if (name == nullptr) return false;
    for (const char *n : names)
        if (strcasecmp(name, n) == 0) return true;
    return false;
}

class ui_element_mac_navidrome : public ui_element_mac {
public:
    service_ptr instantiate(service_ptr ) override {
        return fb2k::wrapNSObject([NavidromeBrowserController new]);
    }
    bool match_name(const char *name) override {
        return matchesLayoutName(name, {"Navidrome", "navidrome-browser"});
    }
    fb2k::stringRef get_name() override { return fb2k::makeString("Navidrome"); }
    GUID get_guid() override { return guid_ui_element_mac; }
};

FB2K_SERVICE_FACTORY(ui_element_mac_navidrome);

class ui_element_mac_navidrome_lyrics : public ui_element_mac {
public:
    service_ptr instantiate(service_ptr ) override {
        return fb2k::wrapNSObject([NavidromeLyricsController new]);
    }
    bool match_name(const char *name) override {
        return matchesLayoutName(name, {"Navidrome Lyrics", "navidrome-lyrics"});
    }
    fb2k::stringRef get_name() override { return fb2k::makeString("Navidrome Lyrics"); }
    GUID get_guid() override { return guid_ui_element_mac_lyrics; }
};

FB2K_SERVICE_FACTORY(ui_element_mac_navidrome_lyrics);
}
