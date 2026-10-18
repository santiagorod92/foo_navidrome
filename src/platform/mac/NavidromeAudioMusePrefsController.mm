#import "NavidromePreferencesController.h"
#include <SDK/cfg_var.h>
#include "../../core/NavidromeAudioMuse.h"
#include "../../core/NavidromeDebugLog.h"
#import "MacI18n.h"

@implementation NavidromeAudioMusePrefsController {
    NSTextField       *_urlField;
    NSSecureTextField *_tokenField;
    NSTextField       *_serverField;
    NSTextField       *_countField;
}

- (instancetype)init {
    self = [super initWithNibName:nil bundle:nil];
    return self;
}

- (void)loadView {
    NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 420, 240)];

    NSTextField *intro = [NSTextField wrappingLabelWithString:
        NDTr("AudioMuse-AI analyses your Navidrome library for Text Search, Instant Playlist and "
             "Song Alchemy (File › AudioMuse-AI, and the Navidrome submenu on tracks).")];
    intro.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:intro];

    _urlField    = [NSTextField textFieldWithString:@""];
    _urlField.placeholderString = @"http://audiomuse:8000";
    _tokenField  = [[NSSecureTextField alloc] init];
    _tokenField.placeholderString = NDTr("only if AudioMuse-AI has auth enabled");
    _serverField = [NSTextField textFieldWithString:@""];
    _serverField.placeholderString = NDTr("only if it serves several media servers");
    _countField  = [NSTextField textFieldWithString:@""];
    NSNumberFormatter *nf = [NSNumberFormatter new];
    nf.minimum = @1; nf.maximum = @(navidrome::audiomuse::kMaxCount); nf.allowsFloats = NO;
    _countField.formatter = nf;

    NSGridView *grid = [NSGridView gridViewWithViews:@[
        @[[NSTextField labelWithString:NDTr("Server URL:")],  _urlField],
        @[[NSTextField labelWithString:NDTr("API token:")],   _tokenField],
        @[[NSTextField labelWithString:NDTr("Server name:")], _serverField],
        @[[NSTextField labelWithString:NDTr("Tracks:")],      _countField],
    ]];
    grid.translatesAutoresizingMaskIntoConstraints = NO;
    [grid columnAtIndex:0].xPlacement = NSGridCellPlacementTrailing;
    grid.rowAlignment = NSGridRowAlignmentFirstBaseline;
    grid.columnSpacing = 8;
    grid.rowSpacing = 8;
    [root addSubview:grid];

    NSTextField *help = [NSTextField wrappingLabelWithString:
        NDTr("“Tracks” also sizes Instant Mix, which needs no AudioMuse-AI settings: Navidrome "
             "answers it itself (from the AudioMuse-AI plugin when installed).")];
    help.translatesAutoresizingMaskIntoConstraints = NO;
    help.textColor = [NSColor secondaryLabelColor];
    help.font = [NSFont systemFontOfSize:11];
    [root addSubview:help];

    [NSLayoutConstraint activateConstraints:@[
        [intro.topAnchor constraintEqualToAnchor:root.topAnchor constant:12],
        [intro.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:16],
        [intro.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-16],
        [grid.topAnchor constraintEqualToAnchor:intro.bottomAnchor constant:12],
        [grid.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:16],
        [grid.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-16],
        [_urlField.widthAnchor constraintGreaterThanOrEqualToConstant:260],
        [_countField.widthAnchor constraintEqualToConstant:60],
        [help.topAnchor constraintEqualToAnchor:grid.bottomAnchor constant:12],
        [help.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:16],
        [help.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-16],
    ]];

    for (NSTextField *f in @[_urlField, _tokenField, _serverField, _countField])
        [[NSNotificationCenter defaultCenter] addObserver:self
                                                 selector:@selector(fieldChanged:)
                                                     name:NSControlTextDidChangeNotification
                                                   object:f];
    self.view = root;
}

- (void)viewDidLoad {
    [super viewDidLoad];
    _urlField.stringValue    = @(navidrome::cfg_audiomuse_url.get().c_str());
    _tokenField.stringValue  = @(navidrome::cfg_audiomuse_token.get().c_str());
    _serverField.stringValue = @(navidrome::cfg_audiomuse_server.get().c_str());
    _countField.stringValue  = [NSString stringWithFormat:@"%d",
        navidrome::audiomuse::clampCount((int)navidrome::cfg_audiomuse_count.get())];
}

- (void)fieldChanged:(NSNotification *)note {
    navidrome::cfg_audiomuse_url.set(_urlField.stringValue.UTF8String ?: "");
    navidrome::cfg_audiomuse_token.set(_tokenField.stringValue.UTF8String ?: "");
    navidrome::cfg_audiomuse_server.set(_serverField.stringValue.UTF8String ?: "");
    const int n = _countField.stringValue.intValue;
    if (n > 0) navidrome::cfg_audiomuse_count.set(navidrome::audiomuse::clampCount(n));
}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
}

@end
