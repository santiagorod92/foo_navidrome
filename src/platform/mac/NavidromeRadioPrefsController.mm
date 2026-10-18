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
#include "../../core/NavidromeGuids.h"
#include "../../core/NavidromeSettings.h"
#include <algorithm>
#include <cstring>
#import "MacI18n.h"

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
    nameCol.title = NDTr("Name");
    nameCol.width = 140;
    [_tableView addTableColumn:nameCol];

    NSTableColumn *urlCol = [[NSTableColumn alloc] initWithIdentifier:@"streamUrl"];
    urlCol.title = NDTr("Stream URL");
    urlCol.width = 220;
    [_tableView addTableColumn:urlCol];

    NSTableColumn *homeCol = [[NSTableColumn alloc] initWithIdentifier:@"homePageUrl"];
    homeCol.title = NDTr("Home Page");
    homeCol.width = 140;
    [_tableView addTableColumn:homeCol];

    scroll.documentView = _tableView;
    [root addSubview:scroll];

    _newButton = [NSButton buttonWithTitle:NDTr("New…") target:self action:@selector(newStation:)];
    _newButton.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:_newButton];

    _editButton = [NSButton buttonWithTitle:NDTr("Edit…") target:self action:@selector(editStation:)];
    _editButton.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:_editButton];

    _deleteButton = [NSButton buttonWithTitle:NDTr("Delete…") target:self action:@selector(deleteStation:)];
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
        _statusLabel.stringValue = NDTr("Not configured");
        return;
    }
    _statusLabel.stringValue = NDTr("Loading…");
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        NSError *err = nil;
        NSArray<SubsonicRadioStation *> *stations =
            [SubsonicClient.sharedClient getRadioStationsWithError:&err];
        dispatch_async(dispatch_get_main_queue(), ^{
            if (err || !stations) {
                self->_statusLabel.stringValue = NDTrf("Failed: {0}", { NDStd(err.localizedDescription ?: NDTr("unknown error")) });
                return;
            }
            self->_stations = stations;
            [self->_tableView reloadData];
            self->_statusLabel.stringValue = stations.count == 0 ? NDTr("No radio stations") : @"";
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
    if (![self promptForRadioStationWithTitle:NDTr("New Radio Station")
                                          name:&name streamURL:&streamUrl homePageURL:&homePageUrl])
        return;
    if (name.length == 0 || streamUrl.length == 0) {
        _statusLabel.stringValue = NDTr("Name and stream URL are required");
        return;
    }
    _statusLabel.stringValue = NDTr("Creating…");
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
            else self->_statusLabel.stringValue = NDTrf("Failed: {0}", { NDStd(err.localizedDescription ?: NDTr("unknown error")) });
        });
    });
}

- (IBAction)editStation:(id)sender {
    NSInteger row = _tableView.selectedRow;
    if (row < 0 || row >= (NSInteger)_stations.count) {
        _statusLabel.stringValue = NDTr("Select a station");
        return;
    }
    SubsonicRadioStation *current = _stations[row];
    NSString *name = nil, *streamUrl = nil, *homePageUrl = nil;
    if (![self promptForRadioStationWithTitle:NDTr("Edit Radio Station")
                                   initialName:current.name ?: @""
                              initialStreamURL:current.streamUrl ?: @""
                            initialHomePageURL:current.homePageUrl ?: @""
                                          name:&name streamURL:&streamUrl homePageURL:&homePageUrl])
        return;
    if (name.length == 0 || streamUrl.length == 0) {
        _statusLabel.stringValue = NDTr("Name and stream URL are required");
        return;
    }
    NSString *stationId = current.stationId;
    _statusLabel.stringValue = NDTr("Updating…");
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
            else self->_statusLabel.stringValue = NDTrf("Failed: {0}", { NDStd(err.localizedDescription ?: NDTr("unknown error")) });
        });
    });
}

- (IBAction)deleteStation:(id)sender {
    NSInteger row = _tableView.selectedRow;
    if (row < 0 || row >= (NSInteger)_stations.count) {
        _statusLabel.stringValue = NDTr("Select a station");
        return;
    }
    SubsonicRadioStation *current = _stations[row];

    NSAlert *confirm = [[NSAlert alloc] init];
    confirm.messageText = NDTrf("Delete “{0}” from the server?", { NDStd(current.name) });
    confirm.informativeText = NDTr("The station is removed for every client.");
    confirm.alertStyle = NSAlertStyleWarning;
    [confirm addButtonWithTitle:NDTr("Delete")];
    [confirm addButtonWithTitle:NDTr("Cancel")];
    if ([confirm runModal] != NSAlertFirstButtonReturn) return;

    NSString *stationId = current.stationId;
    _statusLabel.stringValue = NDTr("Deleting…");
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        NSError *err = nil;
        BOOL ok = [SubsonicClient.sharedClient deleteRadioStation:stationId error:&err];
        if (!ok)
            NAVIDROME_WARN("UI", "delete radio station " + std::string(stationId.UTF8String ?: "") +
                                 ": " + std::string(err.localizedDescription.UTF8String ?: "unknown error"));
        dispatch_async(dispatch_get_main_queue(), ^{
            if (ok) [self refresh];
            else self->_statusLabel.stringValue = NDTrf("Failed: {0}", { NDStd(err.localizedDescription ?: NDTr("unknown error")) });
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
    alert.informativeText = NDTr("Name and stream URL are required. Home page URL is optional.");
    [alert addButtonWithTitle:NDTr("OK")];
    [alert addButtonWithTitle:NDTr("Cancel")];

    CGFloat fieldWidth = 260, rowHeight = 24, rowGap = 6, labelHeight = 16;
    NSView *container = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, fieldWidth, 3 * (rowHeight + labelHeight + rowGap))];

    NSTextField *nameLabel = [NSTextField labelWithString:NDTr("Name:")];
    NSTextField *nameField = [[NSTextField alloc] init];
    nameField.stringValue = initialName ?: @"";

    NSTextField *urlLabel = [NSTextField labelWithString:NDTr("Stream URL:")];
    NSTextField *urlField = [[NSTextField alloc] init];
    urlField.stringValue = initialStreamURL ?: @"";

    NSTextField *homeLabel = [NSTextField labelWithString:NDTr("Home page URL (optional):")];
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
    const char *get_name() override { return navidrome::tr("Radio Stations"); }
    GUID get_guid() override { return navidrome::guids::radioPrefsPage; }
    GUID get_parent_guid() override { return navidrome::guids::prefsPage; }
};

FB2K_SERVICE_FACTORY(preferences_page_navidrome_radio);
}
