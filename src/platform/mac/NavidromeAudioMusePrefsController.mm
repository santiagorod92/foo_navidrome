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
    NSButton          *_keepGoingBox;
    NSButton          *_loudnessBox;
    NSTextField       *_fallbackField;
}

- (instancetype)init {
    self = [super initWithNibName:nil bundle:nil];
    return self;
}

- (void)loadView {
    NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 420, 380)];

    NSTextField *intro = [NSTextField wrappingLabelWithString:
        NDTr("AudioMuse-AI analyses your Navidrome library for Text Search, Instant Playlist, "
             "Song Alchemy and Smart Fade (File › AudioMuse-AI, and the Navidrome submenu on tracks).")];
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

    NSTextField *heading = [NSTextField labelWithString:NDTr("Smart Fade")];
    heading.font = [NSFont boldSystemFontOfSize:[NSFont systemFontSize]];
    heading.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:heading];
    _keepGoingBox = [NSButton checkboxWithTitle:NDTr("Keep the mix going: add songs when a Smart Fade playlist nears its end")
                                         target:self action:@selector(smartFadeChanged:)];
    _loudnessBox = [NSButton checkboxWithTitle:NDTr("Even out loudness between songs (off while ReplayGain is on)")
                                        target:self action:@selector(smartFadeChanged:)];
    _fallbackField = [NSTextField textFieldWithString:@""];
    NSNumberFormatter *fbf = [NSNumberFormatter new];
    fbf.minimum = @0; fbf.maximum = @(navidrome::audiomuse::kMaxFallbackSeconds); fbf.allowsFloats = NO;
    _fallbackField.formatter = fbf;
    NSStackView *fallbackRow = [NSStackView stackViewWithViews:@[
        [NSTextField labelWithString:NDTr("Crossfade when AudioMuse-AI has no transition:")],
        _fallbackField,
        [NSTextField labelWithString:NDTr("seconds (0 = leave it to the other DSPs)")],
    ]];
    fallbackRow.spacing = 6;
    for (NSView *v in @[_keepGoingBox, _loudnessBox, fallbackRow]) {
        v.translatesAutoresizingMaskIntoConstraints = NO;
        [root addSubview:v];
    }

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
        [heading.topAnchor constraintEqualToAnchor:help.bottomAnchor constant:16],
        [heading.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:16],
        [_keepGoingBox.topAnchor constraintEqualToAnchor:heading.bottomAnchor constant:8],
        [_keepGoingBox.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:16],
        [_keepGoingBox.trailingAnchor constraintLessThanOrEqualToAnchor:root.trailingAnchor constant:-16],
        [_loudnessBox.topAnchor constraintEqualToAnchor:_keepGoingBox.bottomAnchor constant:6],
        [_loudnessBox.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:16],
        [_loudnessBox.trailingAnchor constraintLessThanOrEqualToAnchor:root.trailingAnchor constant:-16],
        [fallbackRow.topAnchor constraintEqualToAnchor:_loudnessBox.bottomAnchor constant:8],
        [fallbackRow.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:16],
        [fallbackRow.trailingAnchor constraintLessThanOrEqualToAnchor:root.trailingAnchor constant:-16],
        [_fallbackField.widthAnchor constraintEqualToConstant:40],
    ]];

    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(smartFadeChanged:)
                                                 name:NSControlTextDidChangeNotification
                                               object:_fallbackField];
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
    _keepGoingBox.state = navidrome::cfg_smartfade_keep_going.get() ? NSControlStateValueOn : NSControlStateValueOff;
    _loudnessBox.state = navidrome::cfg_smartfade_loudness.get() ? NSControlStateValueOn : NSControlStateValueOff;
    _fallbackField.stringValue = [NSString stringWithFormat:@"%d",
        navidrome::audiomuse::clampFallbackSeconds((int)navidrome::cfg_smartfade_fallback.get())];
}

- (void)smartFadeChanged:(id)sender {
    navidrome::cfg_smartfade_keep_going.set(_keepGoingBox.state == NSControlStateValueOn);
    navidrome::cfg_smartfade_loudness.set(_loudnessBox.state == NSControlStateValueOn);
    if (_fallbackField.stringValue.length > 0)
        navidrome::cfg_smartfade_fallback.set(navidrome::audiomuse::clampFallbackSeconds(_fallbackField.stringValue.intValue));
    NAVIDROME_LOG("UI", std::string("Smart Fade settings: keep going ") + (navidrome::cfg_smartfade_keep_going.get() ? "on" : "off")
                  + ", loudness " + (navidrome::cfg_smartfade_loudness.get() ? "on" : "off") + ", fallback "
                  + std::to_string(navidrome::cfg_smartfade_fallback.get()) + " s");
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
