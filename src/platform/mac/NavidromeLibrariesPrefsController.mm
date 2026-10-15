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
    GUID get_guid() override { return navidrome::guids::librariesPrefsPage; }
    GUID get_parent_guid() override { return navidrome::guids::prefsPage; }
};

FB2K_SERVICE_FACTORY(preferences_page_navidrome_libsel);
}
