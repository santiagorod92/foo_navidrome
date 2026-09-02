#import "stdafx.h"
#import "SubsonicClient.h"
#import "Mac/NavidromeBrowserController.h"
#import "Mac/NavidromePreferencesController.h"
#include <helpers/advconfig_impl.h>
#include <SDK/cfg_var.h>
#include <SDK/library_manager.h>
#include <SDK/play_callback.h>
#include <SDK/initquit.h>
#include <SDK/ui_element_mac.h>
#include "SubsonicTypes.h"
#include "NavidromePlaylistSync.h"
#include "NavidromeDebugLog.h"
#include <algorithm>
#include <cstring>

// ---------------------------------------------------------------------------
// GUIDs — replace with your own when forking this component
// ---------------------------------------------------------------------------
static constexpr GUID guid_cfg_server_url  = { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01, 0x01 } };
static constexpr GUID guid_cfg_username    = { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01, 0x02 } };
static constexpr GUID guid_cfg_password    = { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01, 0x03 } };
static constexpr GUID guid_cfg_salt        = { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01, 0x04 } };
static constexpr GUID guid_prefs_page      = { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01, 0x05 } };
static constexpr GUID guid_mainmenu_group  = { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01, 0x06 } };
static constexpr GUID guid_mainmenu_cmd    = { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01, 0x07 } };
static constexpr GUID guid_library_viewer  = { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01, 0x08 } };
static constexpr GUID guid_library_prefs   = { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01, 0x09 } };
static constexpr GUID guid_cfg_custom_headers = { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01, 0x0a } };
static constexpr GUID guid_cfg_scrobble    = { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01, 0x0b } };
static constexpr GUID guid_cfg_stream_format = { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01, 0x0c } };
static constexpr GUID guid_cfg_max_bitrate = { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01, 0x0d } };
static constexpr GUID guid_ui_element_mac  = { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01, 0x0e } };
static constexpr GUID guid_radio_prefs_page = { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01, 0x0f } };

// ---------------------------------------------------------------------------
// Config variables (exported so SubsonicClient.mm can access them)
// ---------------------------------------------------------------------------
namespace navidrome {
    cfg_string cfg_server_url(guid_cfg_server_url, "http://navidrome.santirod.local:4533/");
    cfg_string cfg_username  (guid_cfg_username,   "");
    cfg_string cfg_password  (guid_cfg_password,   "");
    cfg_string cfg_salt      (guid_cfg_salt,        "fb2k_navidrome");
    // Extra HTTP headers (one "Name: Value" per line) sent on every request —
    // API, cover art and audio stream. Used e.g. for Cloudflare Access
    // service-token headers when Navidrome sits behind a Zero Trust tunnel.
    cfg_string cfg_custom_headers(guid_cfg_custom_headers, "");
    // Report plays back to Navidrome (play counts, "Recently Played", and any
    // Last.fm / ListenBrainz relay the server has configured).
    // Qualified: an unqualified cfg_bool resolves to the legacy
    // cfg_int_t<bool> (no set()) on the Windows SDK headers, and the two
    // flavours serialize differently — both platforms must use the same one.
    cfg_var_modern::cfg_bool cfg_scrobble(guid_cfg_scrobble, true);

    // Transcoding preferences, applied to every stream.view request.
    // cfg_stream_format: "" = let the server decide, "raw" = never transcode,
    // otherwise a Subsonic format name ("mp3", "opus", "aac", …).
    // cfg_max_bitrate: kbps ceiling; 0 = unlimited.
    // Qualified for the same reason as cfg_scrobble — an unqualified cfg_int
    // resolves to the legacy cfg_int_t<t_int32>, which has no set() and
    // serializes differently.
    cfg_string cfg_stream_format(guid_cfg_stream_format, "");
    cfg_var_modern::cfg_int cfg_max_bitrate(guid_cfg_max_bitrate, 0);
}

// ---------------------------------------------------------------------------
// Scrobbler — reports plays back to Navidrome so play counts, "Recently
// Played" and any Last.fm / ListenBrainz relay configured server-side reflect
// what's played through foobar2000.
//
// Two calls per track, matching the Subsonic contract: submission=false on
// start ("now playing"), submission=true once enough of the track has been
// heard (half its length, capped at 4 minutes — the Last.fm convention).
// ---------------------------------------------------------------------------

namespace {

class navidrome_scrobbler : public play_callback_static {
public:
    unsigned get_flags() override {
        return flag_on_playback_new_track | flag_on_playback_time |
               flag_on_playback_stop;
    }

    void on_playback_new_track(metadb_handle_ptr track) override {
        m_songId.clear();
        m_submitted = false;
        m_length    = 0.0;
        if (track.is_empty()) return;

        const std::string songId = navidrome::trackIdFromURI(track->get_path());
        if (songId.empty()) return;   // not one of ours

        // Deliberately ahead of the scrobble gate: this is a display refresh,
        // not a play report, so it must not follow the scrobbling preference.
        refreshRatingAsync(songId);

        if (!navidrome::cfg_scrobble.get()) return;
        m_songId = songId;
        m_length = track->get_length();
        scrobbleAsync(m_songId, NO);
    }

    void on_playback_time(double time) override {
        if (m_songId.empty() || m_submitted) return;
        // Unknown length (live stream): fall back to the 4-minute cap alone.
        double threshold = (m_length > 0) ? std::min(240.0, m_length * 0.5) : 240.0;
        if (time < threshold) return;
        m_submitted = true;
        scrobbleAsync(m_songId, YES);
    }

    void on_playback_stop(play_control::t_stop_reason) override {
        m_songId.clear();
        m_submitted = false;
    }

    // Unused callbacks (not requested in get_flags, but the interface is pure).
    void on_playback_starting(play_control::t_track_command, bool) override {}
    void on_playback_seek(double) override {}
    void on_playback_pause(bool) override {}
    void on_playback_edited(metadb_handle_ptr) override {}
    void on_playback_dynamic_info(const file_info &) override {}
    void on_playback_dynamic_info_track(const file_info &) override {}
    void on_volume_change(float) override {}

private:
    // Fire and forget on a background queue — a slow or unreachable server must
    // never stall playback, and a failed scrobble isn't worth interrupting for.
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

    // One extra request per played track. That's the only moment we can pick up
    // a rating changed outside foobar (the Navidrome web UI, another client)
    // without polling every playlist entry — Subsonic has no bulk rating
    // lookup, so a whole-playlist refresh would be one request per track.
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

    std::string m_songId;
    double      m_length    = 0.0;
    bool        m_submitted = false;
};

static play_callback_static_factory_t<navidrome_scrobbler> g_navidrome_scrobbler_factory;

// ---------------------------------------------------------------------------
// Startup refresh — brings every playlist entry up to date once per session, so
// a rating column can be sorted on. Grouped by album; see CLAUDE.md for why
// that grouping is what makes it affordable.
// ---------------------------------------------------------------------------

// One-shot dump of the state that shapes every later trace line — logged once
// at startup so a bug report's log says which server / transcode / toggles were
// in effect without a second round-trip.
static void navidromeLogSessionEnv() {
#ifdef NAVIDROME_DEBUG_LOG
    std::string fmt = navidrome::cfg_stream_format.get().c_str();
    NAVIDROME_LOG("Env", std::string("platform=macOS")
        + "  configured=" + ([SubsonicClient.sharedClient isConfigured] ? "yes" : "no")
        + "  server=" + navidrome::cfg_server_url.get().c_str()
        + "  transcode=" + (fmt.empty() ? "server-default" : fmt)
        + "  maxBitrate=" + std::to_string((int)navidrome::cfg_max_bitrate.get())
        + "  scrobble=" + (navidrome::cfg_scrobble.get() ? "on" : "off")
        + "  startupRefresh=" + (navidrome::refreshRatingsOnStartEnabled() ? "on" : "off")
        + "  customHeaders=" + (navidrome::cfg_custom_headers.get().length() ? "yes" : "no"));
#endif
}

static void navidromeRefreshRatingsOnStart() {
    navidromeLogSessionEnv();

    // Main thread: walking the playlists is a main-thread operation.
    navidrome::PlaylistAlbumScan scan = navidrome::scanPlaylistAlbums();


    // Nothing of ours in any playlist — the only exit that stays quiet. Every
    // other one says why, because "hook never fired" and "hook fired and found
    // nothing" are otherwise indistinguishable from the outside.
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

    // Skipping coverage silently is how a partial refresh gets mistaken for a
    // complete one, so the two outcomes that leave entries behind say so.
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
            // One sync for everything: it walks every playlist once, so doing it
            // per album would repeat that walk for no gain.
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

// initquit, not init_stage_callback: the macOS core never dispatches init
// stages, and the failure mode is silence (see CLAUDE.md).
class navidrome_startup_refresh : public initquit {
public:
    void on_init() override { navidromeRefreshRatingsOnStart(); }
};

static initquit_factory_t<navidrome_startup_refresh> g_navidrome_startup_refresh_factory;

} // namespace
// Client calls behind the shared playlist context menu (main.cpp). Background
// thread only — the menu marshals them off the UI thread itself.
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

// ---------------------------------------------------------------------------
// Preferences page (Mac)
// ---------------------------------------------------------------------------

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

} // namespace

// ---------------------------------------------------------------------------
// Radio Stations preferences sub-page — list/add/edit/delete the server's
// configured internet radio stations without opening the browser tree.
// Nested under guid_prefs_page (the main Navidrome credentials page), not
// guid_tools, so it shows as a child of "Navidrome" rather than a sibling.
// Entirely self-contained: own fetch, own NSTableView, calls SubsonicClient's
// radio CRUD methods directly — no dependency on NavidromeBrowserController.
// ---------------------------------------------------------------------------

@interface NavidromeRadioPrefsController : NSViewController <NSTableViewDataSource, NSTableViewDelegate>
@end

@implementation NavidromeRadioPrefsController {
    NSTableView *_tableView;
    NSTextField *_statusLabel;
    NSButton *_newButton, *_editButton, *_deleteButton;
    NSArray<SubsonicRadioStation *> *_stations;
}

- (instancetype)init {
    // No XIB — build UI programmatically in loadView, same as
    // NavidromePreferencesController.
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
        dispatch_async(dispatch_get_main_queue(), ^{
            if (ok) [self refresh];
            else self->_statusLabel.stringValue = [NSString stringWithFormat:@"Failed: %@",
                err.localizedDescription ?: @"unknown error"];
        });
    });
}

// Modal 3-field prompt (name / stream URL / home page URL). Duplicated from
// NavidromeBrowserController rather than shared — this page has no other
// dependency on the browser controller and the two are never built together
// in a way that would make sharing it free.
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
    // Nested under the main Navidrome credentials page, not guid_tools — a
    // child sub-page under "Navidrome" rather than a sibling of it.
    GUID get_parent_guid() override { return guid_prefs_page; }
};

FB2K_SERVICE_FACTORY(preferences_page_navidrome_radio);

// ---------------------------------------------------------------------------
// Main menu: File > Open Navidrome Browser
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Library viewer — exposes the browser to the foobar Media Library system.
// On macOS the visible surface is the preferences sub-page below, registered
// under guid_media_library, which mirrors how Album List / ReFacets show up.
// ---------------------------------------------------------------------------

class library_viewer_navidrome : public library_viewer {
public:
    GUID get_preferences_page() override { return guid_library_prefs; }
    bool have_activate()        override { return true; }

    void activate() override { NavidromeShowStandaloneBrowser(); }

    GUID         get_guid() override { return guid_library_viewer; }
    const char * get_name() override { return "Navidrome"; }
};

static library_viewer_factory_t<library_viewer_navidrome> g_library_viewer_navidrome_factory;

} // namespace

// ---------------------------------------------------------------------------
// Media Library preferences sub-page — embeds the browser directly so users
// see Artists/Albums/Songs without opening a separate window. The page IS
// the browser. A fresh NavidromeBrowserController is created per page mount.
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Native layout panel — lets the browser be docked inside the main window
// layout (Preferences > Display > Layout > Edit Layout > add "Navidrome"),
// as a third mount point alongside the standalone window and the Media
// Library prefs sub-page above. Same VC-per-mount rule applies: the layout
// system may instantiate() more than once (e.g. multiple splits/tabs), so
// each call must return a fresh NavidromeBrowserController, never a shared
// singleton.
// ---------------------------------------------------------------------------

class ui_element_mac_navidrome : public ui_element_mac {
public:
    service_ptr instantiate(service_ptr /*arg*/) override {
        return fb2k::wrapNSObject([NavidromeBrowserController new]);
    }
    bool match_name(const char *name) override {
        return name != nullptr && !strcmp(name, "Navidrome");
    }
    fb2k::stringRef get_name() override { return fb2k::makeString("Navidrome"); }
    GUID get_guid() override { return guid_ui_element_mac; }
};

FB2K_SERVICE_FACTORY(ui_element_mac_navidrome);

} // namespace
