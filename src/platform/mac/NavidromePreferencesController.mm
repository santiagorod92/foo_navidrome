#import "NavidromePreferencesController.h"
#import "SubsonicClient.h"
#import "MacI18n.h"
#include <SDK/cfg_var.h>
#include "../../core/NavidromeAudioMuse.h"
#include "../../core/NavidromeDiagnostics.h"
#include "../../core/NavidromeDebugLog.h"
#include "../../core/NavidromeSettings.h"
#include "../../core/NavidromeLibraryPlatform.h"
#include "../../core/LastFm.h"

static NSArray<NSArray *> *NavidromeStreamFormats(void) {
    NSMutableArray<NSArray *> *out = [NSMutableArray array];
    for (const auto &o : navidrome::streamFormatOptions())
        [out addObject:@[ NDTr(o.label), @(o.value) ]];
    return out;
}

static NSArray<NSNumber *> *NavidromeMaxBitrates(void) {
    NSMutableArray<NSNumber *> *out = [NSMutableArray array];
    for (int kbps : navidrome::maxBitrateOptions())
        [out addObject:@(kbps)];
    return out;
}

@interface NavidromeHeadersEditor : NSObject <NSWindowDelegate>
@property (nonatomic, strong) NSWindow   *window;
@property (nonatomic, strong) NSTextView *textView;
+ (void)show;
@end

static NavidromeHeadersEditor *gHeadersEditor = nil;

@implementation NavidromeHeadersEditor

+ (void)show {
    if (!gHeadersEditor) gHeadersEditor = [NavidromeHeadersEditor new];
    [gHeadersEditor present];
}

- (void)present {
    if (!self.window) [self build];
    self.textView.string =
        [NSString stringWithUTF8String:navidrome::cfg_custom_headers.get().c_str()];
    [self.window center];
    [self.window makeKeyAndOrderFront:nil];
}

- (void)build {
    NSWindow *win = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(0, 0, 520, 360)
        styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                   NSWindowStyleMaskResizable)
        backing:NSBackingStoreBuffered defer:NO];
    win.title = NDTr("Navidrome — Custom HTTP Headers");
    win.releasedWhenClosed = NO;
    win.delegate = self;
    NSView *content = win.contentView;

    NSTextField *hint = [NSTextField wrappingLabelWithString:
        NDTr("One header per line, as  Name: Value  (e.g. for a Cloudflare Zero Trust tunnel).")];
    hint.translatesAutoresizingMaskIntoConstraints = NO;
    hint.textColor = [NSColor secondaryLabelColor];
    hint.font = [NSFont systemFontOfSize:11];
    [content addSubview:hint];

    NSScrollView *scroll = [[NSScrollView alloc] init];
    scroll.translatesAutoresizingMaskIntoConstraints = NO;
    scroll.hasVerticalScroller = YES;
    scroll.borderType = NSBezelBorder;
    NSTextView *tv = [[NSTextView alloc] init];
    tv.minSize = NSMakeSize(0, 0);
    tv.maxSize = NSMakeSize(FLT_MAX, FLT_MAX);
    tv.verticallyResizable = YES;
    tv.horizontallyResizable = NO;
    tv.autoresizingMask = NSViewWidthSizable;
    tv.richText = NO;
    tv.automaticQuoteSubstitutionEnabled = NO;
    tv.automaticDashSubstitutionEnabled = NO;
    tv.font = [NSFont userFixedPitchFontOfSize:12];
    scroll.documentView = tv;
    self.textView = tv;
    [content addSubview:scroll];

    NSButton *cf = [NSButton buttonWithTitle:NDTr("Add Cloudflare headers")
                                      target:self action:@selector(addCloudflare:)];
    cf.translatesAutoresizingMaskIntoConstraints = NO;
    NSButton *save = [NSButton buttonWithTitle:NDTr("Save") target:self action:@selector(save:)];
    save.translatesAutoresizingMaskIntoConstraints = NO;
    save.keyEquivalent = @"\r";
    NSButton *cancel = [NSButton buttonWithTitle:NDTr("Cancel") target:self action:@selector(cancel:)];
    cancel.translatesAutoresizingMaskIntoConstraints = NO;
    [content addSubview:cf];
    [content addSubview:save];
    [content addSubview:cancel];

    CGFloat pad = 14;
    [NSLayoutConstraint activateConstraints:@[
        [hint.topAnchor constraintEqualToAnchor:content.topAnchor constant:pad],
        [hint.leadingAnchor constraintEqualToAnchor:content.leadingAnchor constant:pad],
        [hint.trailingAnchor constraintEqualToAnchor:content.trailingAnchor constant:-pad],

        [scroll.topAnchor constraintEqualToAnchor:hint.bottomAnchor constant:8],
        [scroll.leadingAnchor constraintEqualToAnchor:content.leadingAnchor constant:pad],
        [scroll.trailingAnchor constraintEqualToAnchor:content.trailingAnchor constant:-pad],
        [scroll.bottomAnchor constraintEqualToAnchor:save.topAnchor constant:-pad],

        [cancel.bottomAnchor constraintEqualToAnchor:content.bottomAnchor constant:-pad],
        [cancel.trailingAnchor constraintEqualToAnchor:content.trailingAnchor constant:-pad],
        [save.bottomAnchor constraintEqualToAnchor:content.bottomAnchor constant:-pad],
        [save.trailingAnchor constraintEqualToAnchor:cancel.leadingAnchor constant:-8],
        [cf.bottomAnchor constraintEqualToAnchor:content.bottomAnchor constant:-pad],
        [cf.leadingAnchor constraintEqualToAnchor:content.leadingAnchor constant:pad],
    ]];

    self.window = win;
}

- (void)addCloudflare:(id)sender {
    NSMutableString *s = [self.textView.string mutableCopy] ?: [NSMutableString string];
    NSString *lower = s.lowercaseString;
    for (NSString *name in @[@"CF-Access-Client-Id", @"CF-Access-Client-Secret"]) {
        if ([lower rangeOfString:name.lowercaseString].location != NSNotFound) continue;
        if (s.length && ![s hasSuffix:@"\n"]) [s appendString:@"\n"];
        [s appendFormat:@"%@: \n", name];
    }
    self.textView.string = s;
}

- (void)save:(id)sender {
    navidrome::cfg_custom_headers.set([self.textView.string UTF8String] ?: "");
    [self.window orderOut:nil];
}

- (void)cancel:(id)sender {
    [self.window orderOut:nil];
}

@end

@interface NavidromePreferencesController ()
@property (nonatomic, strong) NSPopUpButton      *profilePopup;
@property (nonatomic, strong) NSButton           *profileAddButton;
@property (nonatomic, strong) NSButton           *profileRemoveButton;
@property (nonatomic, strong) NSTextField        *serverField;
@property (nonatomic, strong) NSTextField        *usernameField;
@property (nonatomic, strong) NSSecureTextField  *passwordField;
@property (nonatomic, strong) NSTextField        *statusLabel;
@property (nonatomic, strong) NSButton           *testButton;
@property (nonatomic, strong) NSButton           *headersButton;
@property (nonatomic, strong) NSButton           *scrobbleCheckbox;
@property (nonatomic, strong) NSButton           *rescanButton;
@property (nonatomic, strong) NSTextField        *scanStatusLabel;
@property (nonatomic, strong) NSButton           *diagButton;
@property (nonatomic, strong) NSButton           *logFolderButton;
@property (nonatomic, strong) NSTextField        *diagStatusLabel;
@property (nonatomic, strong) NSPopUpButton      *formatPopup;
@property (nonatomic, strong) NSPopUpButton      *bitratePopup;
@property (nonatomic, strong) NSPopUpButton      *languagePopup;
@property (nonatomic, strong) NSSecureTextField  *lastFmKeyField;
@end

@implementation NavidromePreferencesController

- (instancetype)init {
    self = [super initWithNibName:nil bundle:nil];
    return self;
}

- (void)loadView {
    NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 400, 260)];

    auto makeLabel = ^NSTextField *(NSString *text) {
        NSTextField *lbl = [NSTextField labelWithString:text];
        lbl.translatesAutoresizingMaskIntoConstraints = NO;
        lbl.alignment = NSTextAlignmentRight;
        [root addSubview:lbl];
        return lbl;
    };

    auto makeField = ^NSTextField *(NSString *placeholder) {
        NSTextField *f = [[NSTextField alloc] init];
        f.translatesAutoresizingMaskIntoConstraints = NO;
        f.placeholderString = placeholder;
        [root addSubview:f];
        return f;
    };

    auto makeSecure = ^NSSecureTextField *(NSString *placeholder) {
        NSSecureTextField *f = [[NSSecureTextField alloc] init];
        f.translatesAutoresizingMaskIntoConstraints = NO;
        f.placeholderString = placeholder;
        [root addSubview:f];
        return f;
    };

    NSTextField *lProfile  = makeLabel(NDTr("Server:"));
    NSTextField *lServer   = makeLabel(NDTr("Server URL:"));
    NSTextField *lUser     = makeLabel(NDTr("Username:"));
    NSTextField *lPassword = makeLabel(NDTr("Password:"));

    _serverField   = makeField(@"http://localhost:4533/");
    _usernameField = makeField(@"admin");
    _passwordField = makeSecure(@"••••••");

    _profilePopup = [[NSPopUpButton alloc] init];
    _profilePopup.translatesAutoresizingMaskIntoConstraints = NO;
    _profilePopup.target = self;
    _profilePopup.action = @selector(profileSelected:);
    [root addSubview:_profilePopup];

    _profileAddButton = [NSButton buttonWithTitle:NDTr("Add…")
                                           target:self
                                           action:@selector(addProfile:)];
    _profileAddButton.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:_profileAddButton];

    _profileRemoveButton = [NSButton buttonWithTitle:NDTr("Remove")
                                              target:self
                                              action:@selector(removeProfile:)];
    _profileRemoveButton.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:_profileRemoveButton];

    _testButton = [NSButton buttonWithTitle:NDTr("Test Connection")
                                     target:self
                                     action:@selector(testConnection:)];
    _testButton.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:_testButton];

    _headersButton = [NSButton buttonWithTitle:NDTr("Custom Headers…")
                                        target:self
                                        action:@selector(openCustomHeaders:)];
    _headersButton.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:_headersButton];

    _scrobbleCheckbox = [NSButton checkboxWithTitle:NDTr("Report plays to Navidrome (scrobbling)")
                                             target:self
                                             action:@selector(scrobbleToggled:)];
    _scrobbleCheckbox.translatesAutoresizingMaskIntoConstraints = NO;
    _scrobbleCheckbox.toolTip = NDTr("Updates play counts and “Recently Played” on the "
                                      "server, and feeds any Last.fm / ListenBrainz relay it has "
                                      "configured.");
    [root addSubview:_scrobbleCheckbox];

    NSTextField *lFormat  = makeLabel(NDTr("Stream as:"));
    NSTextField *lBitrate = makeLabel(NDTr("Max bitrate:"));

    _formatPopup = [[NSPopUpButton alloc] init];
    _formatPopup.translatesAutoresizingMaskIntoConstraints = NO;
    for (NSArray *entry in NavidromeStreamFormats())
        [_formatPopup addItemWithTitle:entry[0]];
    _formatPopup.target = self;
    _formatPopup.action = @selector(transcodeChanged:);
    _formatPopup.toolTip = NDTr("Ask the server to transcode on the fly. "
                                 "Useful on slow links; “Original” always sends the stored file. "
                                 "The server must have a transcoding configured for the chosen "
                                 "format — Navidrome ships MP3, Opus and AAC; FLAC and WAV have "
                                 "to be added in its admin UI.");
    [root addSubview:_formatPopup];

    _bitratePopup = [[NSPopUpButton alloc] init];
    _bitratePopup.translatesAutoresizingMaskIntoConstraints = NO;
    for (NSNumber *kbps in NavidromeMaxBitrates())
        [_bitratePopup addItemWithTitle:kbps.integerValue == 0
            ? NDTr("Unlimited")
            : [NSString stringWithFormat:@"%ld kbps", (long)kbps.integerValue]];
    _bitratePopup.target = self;
    _bitratePopup.action = @selector(transcodeChanged:);
    _bitratePopup.toolTip = NDTr("Upper bound the server may not exceed. "
                                  "Ignored when the stream isn't transcoded, and when the "
                                  "target format is lossless (FLAC / WAV).");
    [root addSubview:_bitratePopup];

    NSTextField *lLanguage = makeLabel(NDTr("Language:"));
    _languagePopup = [[NSPopUpButton alloc] init];
    _languagePopup.translatesAutoresizingMaskIntoConstraints = NO;
    for (const auto &o : navidrome::i18n::languageOptions())
        [_languagePopup addItemWithTitle:NDTr(o.label)];
    _languagePopup.target = self;
    _languagePopup.action = @selector(languageChanged:);
    [root addSubview:_languagePopup];

    NSTextField *languageHint = [NSTextField labelWithString:NDTr("Restart foobar2000 to apply.")];
    languageHint.translatesAutoresizingMaskIntoConstraints = NO;
    languageHint.textColor = [NSColor secondaryLabelColor];
    languageHint.font = [NSFont systemFontOfSize:11];
    [root addSubview:languageHint];

    NSTextField *lLastFm = makeLabel(NDTr("Last.fm API key:"));
    _lastFmKeyField = makeSecure(@"");

    NSTextField *lastFmHint = [NSTextField labelWithString:NDTr("Optional. Fills in artist biographies the server has none for.")];
    lastFmHint.translatesAutoresizingMaskIntoConstraints = NO;
    lastFmHint.textColor = [NSColor secondaryLabelColor];
    lastFmHint.font = [NSFont systemFontOfSize:11];
    [root addSubview:lastFmHint];

    _rescanButton = [NSButton buttonWithTitle:NDTr("Rescan")
                                        target:self
                                        action:@selector(rescanLibrary:)];
    _rescanButton.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:_rescanButton];

    _scanStatusLabel = [NSTextField labelWithString:@""];
    _scanStatusLabel.translatesAutoresizingMaskIntoConstraints = NO;
    _scanStatusLabel.textColor = [NSColor secondaryLabelColor];
    _scanStatusLabel.font = [NSFont systemFontOfSize:11];
    _scanStatusLabel.lineBreakMode = NSLineBreakByTruncatingTail;
    [_scanStatusLabel setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow
                                               forOrientation:NSLayoutConstraintOrientationHorizontal];
    [root addSubview:_scanStatusLabel];

    NSTextField *(^makeSection)(NSString *) = ^NSTextField *(NSString *title) {
        NSTextField *t = [NSTextField labelWithString:title];
        t.translatesAutoresizingMaskIntoConstraints = NO;
        t.font = [NSFont boldSystemFontOfSize:NSFont.systemFontSize];
        [root addSubview:t];
        return t;
    };
    NSBox *(^makeLine)(void) = ^NSBox *(void) {
        NSBox *line = [[NSBox alloc] init];
        line.boxType = NSBoxSeparator;
        line.translatesAutoresizingMaskIntoConstraints = NO;
        [root addSubview:line];
        return line;
    };
    NSTextField *connSection    = makeSection(NDTr("Navidrome Server Connection"));
    NSBox       *connLine       = makeLine();
    NSTextField *librarySection = makeSection(NDTr("Rescan Navidrome Library"));
    NSBox       *libraryLine    = makeLine();
    NSTextField *logsSection    = makeSection(NDTr("Logs and Troubleshooting"));
    NSBox       *logsLine       = makeLine();

    _diagButton = [NSButton buttonWithTitle:NDTr("Copy Diagnostics")
                                     target:self
                                     action:@selector(copyDiagnostics:)];
    _diagButton.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:_diagButton];

    _logFolderButton = [NSButton buttonWithTitle:NDTr("Show Log in Finder")
                                          target:self
                                          action:@selector(showLogInFinder:)];
    _logFolderButton.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:_logFolderButton];

    for (NSButton *b in @[ _rescanButton, _diagButton, _logFolderButton ])
        [b setContentHuggingPriority:NSLayoutPriorityDefaultHigh
                      forOrientation:NSLayoutConstraintOrientationHorizontal];

    _diagStatusLabel = [NSTextField labelWithString:@""];
    _diagStatusLabel.translatesAutoresizingMaskIntoConstraints = NO;
    _diagStatusLabel.textColor = [NSColor secondaryLabelColor];
    _diagStatusLabel.font = [NSFont systemFontOfSize:11];
    _diagStatusLabel.lineBreakMode = NSLineBreakByTruncatingTail;
    [_diagStatusLabel setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow
                                               forOrientation:NSLayoutConstraintOrientationHorizontal];
    [root addSubview:_diagStatusLabel];

    _statusLabel = [NSTextField labelWithString:@""];
    _statusLabel.translatesAutoresizingMaskIntoConstraints = NO;
    _statusLabel.textColor = [NSColor secondaryLabelColor];
    _statusLabel.font = [NSFont systemFontOfSize:11];
    [root addSubview:_statusLabel];

    NSTextField *creditLabel = [NSTextField labelWithString:
        [NSString stringWithFormat:@"%@\n%s", NDTr(navidrome::kPrefsAuthorLine), navidrome::kSourceCodeUrl]];
    creditLabel.translatesAutoresizingMaskIntoConstraints = NO;
    creditLabel.textColor = [NSColor tertiaryLabelColor];
    creditLabel.font = [NSFont systemFontOfSize:10];
    creditLabel.selectable = YES;
    [root addSubview:creditLabel];

    CGFloat pad   = 16;
    CGFloat vGap  = 10;
    CGFloat labelW = 120;
    CGFloat fieldH = 22;

    [NSLayoutConstraint activateConstraints:@[
        [connSection.topAnchor constraintEqualToAnchor:root.topAnchor constant:pad],
        [connSection.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:pad],
        [connLine.centerYAnchor constraintEqualToAnchor:connSection.centerYAnchor],
        [connLine.leadingAnchor constraintEqualToAnchor:connSection.trailingAnchor constant:8],
        [connLine.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-pad],

        [lProfile.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:pad],
        [lProfile.widthAnchor constraintEqualToConstant:labelW],
        [lProfile.centerYAnchor constraintEqualToAnchor:_profilePopup.centerYAnchor],

        [_profilePopup.topAnchor constraintEqualToAnchor:connSection.bottomAnchor constant:vGap],
        [_profilePopup.leadingAnchor constraintEqualToAnchor:lProfile.trailingAnchor constant:8],
        [_profilePopup.widthAnchor constraintGreaterThanOrEqualToConstant:180],
        [_profileAddButton.centerYAnchor constraintEqualToAnchor:_profilePopup.centerYAnchor],
        [_profileAddButton.leadingAnchor constraintEqualToAnchor:_profilePopup.trailingAnchor constant:8],
        [_profileRemoveButton.centerYAnchor constraintEqualToAnchor:_profilePopup.centerYAnchor],
        [_profileRemoveButton.leadingAnchor constraintEqualToAnchor:_profileAddButton.trailingAnchor constant:4],
        [_profileRemoveButton.trailingAnchor constraintLessThanOrEqualToAnchor:root.trailingAnchor constant:-pad],

        [lServer.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:pad],
        [lServer.widthAnchor constraintEqualToConstant:labelW],
        [lServer.centerYAnchor constraintEqualToAnchor:_serverField.centerYAnchor],

        [_serverField.topAnchor constraintEqualToAnchor:_profilePopup.bottomAnchor constant:vGap],
        [_serverField.leadingAnchor constraintEqualToAnchor:lServer.trailingAnchor constant:8],
        [_serverField.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-pad],
        [_serverField.heightAnchor constraintEqualToConstant:fieldH],

        [lUser.topAnchor constraintEqualToAnchor:_serverField.bottomAnchor constant:vGap],
        [lUser.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:pad],
        [lUser.widthAnchor constraintEqualToConstant:labelW],
        [lUser.centerYAnchor constraintEqualToAnchor:_usernameField.centerYAnchor],

        [_usernameField.topAnchor constraintEqualToAnchor:_serverField.bottomAnchor constant:vGap],
        [_usernameField.leadingAnchor constraintEqualToAnchor:lUser.trailingAnchor constant:8],
        [_usernameField.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-pad],
        [_usernameField.heightAnchor constraintEqualToConstant:fieldH],

        [lPassword.topAnchor constraintEqualToAnchor:_usernameField.bottomAnchor constant:vGap],
        [lPassword.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:pad],
        [lPassword.widthAnchor constraintEqualToConstant:labelW],
        [lPassword.centerYAnchor constraintEqualToAnchor:_passwordField.centerYAnchor],

        [_passwordField.topAnchor constraintEqualToAnchor:_usernameField.bottomAnchor constant:vGap],
        [_passwordField.leadingAnchor constraintEqualToAnchor:lPassword.trailingAnchor constant:8],
        [_passwordField.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-pad],
        [_passwordField.heightAnchor constraintEqualToConstant:fieldH],

        [_testButton.topAnchor constraintEqualToAnchor:_passwordField.bottomAnchor constant:vGap * 1.5],
        [_testButton.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:pad + labelW + 8],

        [_statusLabel.centerYAnchor constraintEqualToAnchor:_testButton.centerYAnchor],
        [_statusLabel.leadingAnchor constraintEqualToAnchor:_testButton.trailingAnchor constant:10],
        [_statusLabel.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-pad],

        [_headersButton.topAnchor constraintEqualToAnchor:_testButton.bottomAnchor constant:vGap],
        [_headersButton.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:pad + labelW + 8],

        [_scrobbleCheckbox.topAnchor constraintEqualToAnchor:_headersButton.bottomAnchor constant:vGap],
        [_scrobbleCheckbox.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:pad + labelW + 8],

        [lFormat.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:pad],
        [lFormat.widthAnchor constraintEqualToConstant:labelW],
        [lFormat.centerYAnchor constraintEqualToAnchor:_formatPopup.centerYAnchor],

        [_formatPopup.topAnchor constraintEqualToAnchor:_scrobbleCheckbox.bottomAnchor constant:vGap],
        [_formatPopup.leadingAnchor constraintEqualToAnchor:lFormat.trailingAnchor constant:8],

        [lBitrate.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:pad],
        [lBitrate.widthAnchor constraintEqualToConstant:labelW],
        [lBitrate.centerYAnchor constraintEqualToAnchor:_bitratePopup.centerYAnchor],

        [_bitratePopup.topAnchor constraintEqualToAnchor:_formatPopup.bottomAnchor constant:vGap],
        [_bitratePopup.leadingAnchor constraintEqualToAnchor:lBitrate.trailingAnchor constant:8],

        [lLanguage.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:pad],
        [lLanguage.widthAnchor constraintEqualToConstant:labelW],
        [lLanguage.centerYAnchor constraintEqualToAnchor:_languagePopup.centerYAnchor],

        [_languagePopup.topAnchor constraintEqualToAnchor:_bitratePopup.bottomAnchor constant:vGap],
        [_languagePopup.leadingAnchor constraintEqualToAnchor:lLanguage.trailingAnchor constant:8],
        [languageHint.topAnchor constraintEqualToAnchor:_languagePopup.bottomAnchor constant:4],
        [languageHint.leadingAnchor constraintEqualToAnchor:_languagePopup.leadingAnchor constant:2],
        [languageHint.trailingAnchor constraintLessThanOrEqualToAnchor:root.trailingAnchor constant:-pad],

        [lLastFm.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:pad],
        [lLastFm.widthAnchor constraintEqualToConstant:labelW],
        [lLastFm.centerYAnchor constraintEqualToAnchor:_lastFmKeyField.centerYAnchor],

        [_lastFmKeyField.topAnchor constraintEqualToAnchor:languageHint.bottomAnchor constant:vGap],
        [_lastFmKeyField.leadingAnchor constraintEqualToAnchor:lLastFm.trailingAnchor constant:8],
        [_lastFmKeyField.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-pad],
        [_lastFmKeyField.heightAnchor constraintEqualToConstant:fieldH],
        [lastFmHint.topAnchor constraintEqualToAnchor:_lastFmKeyField.bottomAnchor constant:4],
        [lastFmHint.leadingAnchor constraintEqualToAnchor:_lastFmKeyField.leadingAnchor constant:2],
        [lastFmHint.trailingAnchor constraintLessThanOrEqualToAnchor:root.trailingAnchor constant:-pad],

        [librarySection.topAnchor constraintEqualToAnchor:lastFmHint.bottomAnchor constant:vGap * 2],
        [librarySection.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:pad],
        [libraryLine.centerYAnchor constraintEqualToAnchor:librarySection.centerYAnchor],
        [libraryLine.leadingAnchor constraintEqualToAnchor:librarySection.trailingAnchor constant:8],
        [libraryLine.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-pad],

        [_rescanButton.topAnchor constraintEqualToAnchor:librarySection.bottomAnchor constant:vGap],
        [_rescanButton.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:pad + labelW + 8],

        [_scanStatusLabel.centerYAnchor constraintEqualToAnchor:_rescanButton.centerYAnchor],
        [_scanStatusLabel.leadingAnchor constraintEqualToAnchor:_rescanButton.trailingAnchor constant:10],
        [_scanStatusLabel.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-pad],

        [logsSection.topAnchor constraintEqualToAnchor:_rescanButton.bottomAnchor constant:vGap * 2],
        [logsSection.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:pad],
        [logsLine.centerYAnchor constraintEqualToAnchor:logsSection.centerYAnchor],
        [logsLine.leadingAnchor constraintEqualToAnchor:logsSection.trailingAnchor constant:8],
        [logsLine.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-pad],

        [_diagButton.topAnchor constraintEqualToAnchor:logsSection.bottomAnchor constant:vGap],
        [_diagButton.leadingAnchor constraintEqualToAnchor:_rescanButton.leadingAnchor],
        [_logFolderButton.centerYAnchor constraintEqualToAnchor:_diagButton.centerYAnchor],
        [_logFolderButton.leadingAnchor constraintEqualToAnchor:_diagButton.trailingAnchor constant:8],
        [_diagStatusLabel.centerYAnchor constraintEqualToAnchor:_diagButton.centerYAnchor],
        [_diagStatusLabel.leadingAnchor constraintEqualToAnchor:_logFolderButton.trailingAnchor constant:10],
        [_diagStatusLabel.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-pad],

        [creditLabel.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:pad],
        [creditLabel.topAnchor constraintGreaterThanOrEqualToAnchor:_diagButton.bottomAnchor constant:vGap * 2],
    ]];
    NSLayoutConstraint *creditBottom =
        [creditLabel.bottomAnchor constraintEqualToAnchor:root.bottomAnchor constant:-pad];
    creditBottom.priority = NSLayoutPriorityDefaultLow;
    creditBottom.active = YES;

    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(fieldChanged:)
                                                 name:NSControlTextDidChangeNotification
                                               object:_serverField];
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(fieldChanged:)
                                                 name:NSControlTextDidChangeNotification
                                               object:_usernameField];
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(fieldChanged:)
                                                 name:NSControlTextDidChangeNotification
                                               object:_passwordField];
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(lastFmKeyChanged:)
                                                 name:NSControlTextDidChangeNotification
                                               object:_lastFmKeyField];

    self.view = root;
}

- (void)viewDidLoad {
    [super viewDidLoad];
    [self loadSettings];
    [self loadProfiles];
}

- (void)loadProfiles {
    const navidrome::ServerProfileState state = navidrome::currentServerProfiles();
    [_profilePopup removeAllItems];
    for (const auto &p : state.profiles)
        [_profilePopup addItemWithTitle:@(p.name.c_str())];
    [_profilePopup selectItemWithTitle:@(state.active.c_str())];
    _profileRemoveButton.enabled = state.profiles.size() > 1;
}

- (void)serverProfileChanged {
    [self loadSettings];
    [self loadProfiles];
    _statusLabel.stringValue = @"";
}

- (IBAction)profileSelected:(id)sender {
    NSString *title = _profilePopup.titleOfSelectedItem;
    if (!title) return;
    if (navidrome::selectServerProfile(NDStd(title))) [self serverProfileChanged];
    else [self loadProfiles];
}

- (IBAction)addProfile:(id)sender {
    std::string name;
    if (!navidrome::promptForText(navidrome::tr("Add Server"), navidrome::tr("Name of the new server:"), name) ||
        name.empty())
        return;
    navidrome::addNewServerProfile(name);
    [self serverProfileChanged];
    [self.view.window makeFirstResponder:_serverField];
}

- (IBAction)removeProfile:(id)sender {
    const navidrome::ServerProfileState state = navidrome::currentServerProfiles();
    if (state.profiles.size() < 2) return;
    NSAlert *confirm = [[NSAlert alloc] init];
    confirm.messageText = NDTrf("Remove the server “{0}” and its saved login?", { state.active });
    confirm.alertStyle = NSAlertStyleWarning;
    [confirm addButtonWithTitle:NDTr("Remove")];
    [confirm addButtonWithTitle:NDTr("Cancel")];
    if ([confirm runModal] != NSAlertFirstButtonReturn) return;
    navidrome::removeCurrentServerProfile();
    [self serverProfileChanged];
}

- (void)loadSettings {
    _serverField.stringValue   = [NSString stringWithUTF8String:navidrome::cfg_server_url.get().c_str()];
    _usernameField.stringValue = [NSString stringWithUTF8String:navidrome::cfg_username.get().c_str()];
    _passwordField.stringValue = [NSString stringWithUTF8String:navidrome::cfg_password.get().c_str()];
    _scrobbleCheckbox.state    = navidrome::cfg_scrobble.get() ? NSControlStateValueOn
                                                               : NSControlStateValueOff;

    NSString *format = [NSString stringWithUTF8String:navidrome::cfg_stream_format.get().c_str()];
    NSArray<NSArray *> *formats = NavidromeStreamFormats();
    NSInteger formatIndex = 0;
    for (NSUInteger i = 0; i < formats.count; i++)
        if ([formats[i][1] isEqualToString:format]) { formatIndex = (NSInteger)i; break; }
    [_formatPopup selectItemAtIndex:formatIndex];

    NSInteger bitrate = (NSInteger)navidrome::cfg_max_bitrate.get();
    NSArray<NSNumber *> *rates = NavidromeMaxBitrates();
    NSInteger bitrateIndex = 0;
    for (NSUInteger i = 0; i < rates.count; i++)
        if (rates[i].integerValue == bitrate) { bitrateIndex = (NSInteger)i; break; }
    [_bitratePopup selectItemAtIndex:bitrateIndex];

    [_languagePopup selectItemAtIndex:navidrome::i18n::languageOptionIndex(navidrome::cfg_ui_language.get().c_str())];
    _lastFmKeyField.stringValue = [NSString stringWithUTF8String:navidrome::cfg_lastfm_api_key.get().c_str()];
}

- (void)lastFmKeyChanged:(NSNotification *)note {
    navidrome::cfg_lastfm_api_key.set(navidrome::lastfm::trimSpaces([_lastFmKeyField.stringValue UTF8String] ?: "").c_str());
}

- (IBAction)languageChanged:(id)sender {
    const auto &langs = navidrome::i18n::languageOptions();
    NSInteger li = _languagePopup.indexOfSelectedItem;
    if (li < 0 || li >= (NSInteger)langs.size()) return;
    const char *value = langs[(std::size_t)li].value;
    NAVIDROME_NOTE("Env", std::string("UI language set to \"") + value + "\" (applies after restart)");
    navidrome::cfg_ui_language.set(value);
}

- (IBAction)scrobbleToggled:(id)sender {
    navidrome::cfg_scrobble.set(_scrobbleCheckbox.state == NSControlStateValueOn);
}

- (IBAction)transcodeChanged:(id)sender {
    NSArray<NSArray *> *formats = NavidromeStreamFormats();
    NSInteger fi = _formatPopup.indexOfSelectedItem;
    if (fi >= 0 && fi < (NSInteger)formats.count)
        navidrome::cfg_stream_format.set([formats[(NSUInteger)fi][1] UTF8String]);

    NSArray<NSNumber *> *rates = NavidromeMaxBitrates();
    NSInteger bi = _bitratePopup.indexOfSelectedItem;
    if (bi >= 0 && bi < (NSInteger)rates.count)
        navidrome::cfg_max_bitrate.set(rates[(NSUInteger)bi].integerValue);
}

- (void)fieldChanged:(NSNotification *)note {
    [self saveSettings];
}

- (void)saveSettings {
    navidrome::cfg_server_url.set([_serverField.stringValue UTF8String] ?: "");
    navidrome::cfg_username.set  ([_usernameField.stringValue UTF8String] ?: "");
    navidrome::cfg_password.set  ([_passwordField.stringValue UTF8String] ?: "");
    [[SubsonicClient sharedClient] refreshMusicFolders];
}

- (IBAction)testConnection:(id)sender {
    [self saveSettings];
    _testButton.enabled = NO;
    _statusLabel.stringValue = NDTr("Testing…");
    _statusLabel.textColor = [NSColor secondaryLabelColor];

    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        NSError *err = nil;
        BOOL ok = [SubsonicClient.sharedClient pingWithError:&err];
        dispatch_async(dispatch_get_main_queue(), ^{
            self->_testButton.enabled = YES;
            if (ok) {
                self->_statusLabel.stringValue = NDTr("Connected!");
                self->_statusLabel.textColor = [NSColor systemGreenColor];
            } else {
                self->_statusLabel.stringValue = [NSString stringWithFormat:@"%@",
                    err.localizedDescription ?: NDTr("Connection failed")];
                self->_statusLabel.textColor = [NSColor systemRedColor];
            }
        });
    });
}

- (IBAction)rescanLibrary:(id)sender {
    _rescanButton.enabled = NO;
    _scanStatusLabel.textColor = [NSColor secondaryLabelColor];
    _scanStatusLabel.stringValue = NDTr("Starting scan…");

    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        std::string err;
        navidrome::ScanStatus status = navidrome::libraryCore().runLibraryScan(
            [self](const navidrome::ScanStatus &st) {
                NSString *text = @(navidrome::scanProgressText(st.count).c_str());
                dispatch_async(dispatch_get_main_queue(), ^{
                    self->_scanStatusLabel.stringValue = text;
                });
            }, err);
        const bool ok = err.empty();
        NSString *text = @((ok ? navidrome::scanCompleteText(status.count)
                               : navidrome::scanFailedText(err)).c_str());
        dispatch_async(dispatch_get_main_queue(), ^{
            self->_rescanButton.enabled = YES;
            self->_scanStatusLabel.textColor = ok ? [NSColor systemGreenColor] : [NSColor systemRedColor];
            self->_scanStatusLabel.stringValue = text;
        });
    });
}

- (IBAction)copyDiagnostics:(id)sender {
    _diagButton.enabled = NO;
    _diagStatusLabel.stringValue = NDTr("Collecting…");
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        std::string text;
        navidrome::dbg::runGuarded("UI", "copy diagnostics", [&] { text = navidrome::collectDiagnostics(); });
        NSString *s = [NSString stringWithUTF8String:text.c_str()];
        dispatch_async(dispatch_get_main_queue(), ^{
            self->_diagButton.enabled = YES;
            NSPasteboard *pb = NSPasteboard.generalPasteboard;
            [pb clearContents];
            const BOOL ok = s.length > 0 && [pb setString:s forType:NSPasteboardTypeString];
            if (!ok) NAVIDROME_WARN("UI", "copy diagnostics: pasteboard write failed");
            self->_diagStatusLabel.stringValue = ok ? NDTr("Copied to the clipboard")
                                                    : NDTr("Couldn't copy the diagnostics");
        });
    });
}

- (IBAction)showLogInFinder:(id)sender {
    const std::string path = navidrome::componentLogPath();
    if (path.empty()) { _diagStatusLabel.stringValue = NDTr("No log file yet"); return; }
    NSURL *url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
    if ([NSFileManager.defaultManager fileExistsAtPath:url.path])
        [NSWorkspace.sharedWorkspace activateFileViewerSelectingURLs:@[ url ]];
    else
        [NSWorkspace.sharedWorkspace openURL:url.URLByDeletingLastPathComponent];
}

- (IBAction)openCustomHeaders:(id)sender {
    [NavidromeHeadersEditor show];
}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
}

@end
