#import "NavidromePreferencesController.h"
#import "SubsonicClient.h"
#include <SDK/cfg_var.h>
#include "../../core/NavidromeAudioMuse.h"
#include "../../core/NavidromeDiagnostics.h"
#include "../../core/NavidromeDebugLog.h"

namespace navidrome {

    extern cfg_string cfg_server_url;
    extern cfg_string cfg_username;
    extern cfg_string cfg_password;
    extern cfg_string cfg_salt;
    extern cfg_string cfg_custom_headers;
    extern cfg_var_modern::cfg_bool cfg_scrobble;
    extern cfg_string cfg_stream_format;
    extern cfg_var_modern::cfg_int cfg_max_bitrate;
}

static NSArray<NSArray *> *NavidromeStreamFormats(void) {
    NSMutableArray<NSArray *> *out = [NSMutableArray array];
    for (const auto &o : navidrome::streamFormatOptions())
        [out addObject:@[ @(o.label), @(o.value) ]];
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
    win.title = @"Navidrome — Custom HTTP Headers";
    win.releasedWhenClosed = NO;
    win.delegate = self;
    NSView *content = win.contentView;

    NSTextField *hint = [NSTextField wrappingLabelWithString:
        @"One header per line, as  Name: Value  (e.g. for a Cloudflare Zero Trust tunnel)."];
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

    NSButton *cf = [NSButton buttonWithTitle:@"Add Cloudflare headers"
                                      target:self action:@selector(addCloudflare:)];
    cf.translatesAutoresizingMaskIntoConstraints = NO;
    NSButton *save = [NSButton buttonWithTitle:@"Save" target:self action:@selector(save:)];
    save.translatesAutoresizingMaskIntoConstraints = NO;
    save.keyEquivalent = @"\r";
    NSButton *cancel = [NSButton buttonWithTitle:@"Cancel" target:self action:@selector(cancel:)];
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

    NSTextField *lServer   = makeLabel(@"Server URL:");
    NSTextField *lUser     = makeLabel(@"Username:");
    NSTextField *lPassword = makeLabel(@"Password:");

    _serverField   = makeField(@"http://navidrome.santirod.local:4533/");
    _usernameField = makeField(@"admin");
    _passwordField = makeSecure(@"••••••");

    _testButton = [NSButton buttonWithTitle:@"Test Connection"
                                     target:self
                                     action:@selector(testConnection:)];
    _testButton.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:_testButton];

    _headersButton = [NSButton buttonWithTitle:@"Custom Headers…"
                                        target:self
                                        action:@selector(openCustomHeaders:)];
    _headersButton.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:_headersButton];

    _scrobbleCheckbox = [NSButton checkboxWithTitle:@"Report plays to Navidrome (scrobbling)"
                                             target:self
                                             action:@selector(scrobbleToggled:)];
    _scrobbleCheckbox.translatesAutoresizingMaskIntoConstraints = NO;
    _scrobbleCheckbox.toolTip = @"Updates play counts and “Recently Played” on the "
                                 "server, and feeds any Last.fm / ListenBrainz relay it has "
                                 "configured.";
    [root addSubview:_scrobbleCheckbox];

    NSTextField *lFormat  = makeLabel(@"Stream as:");
    NSTextField *lBitrate = makeLabel(@"Max bitrate:");

    _formatPopup = [[NSPopUpButton alloc] init];
    _formatPopup.translatesAutoresizingMaskIntoConstraints = NO;
    for (NSArray *entry in NavidromeStreamFormats())
        [_formatPopup addItemWithTitle:entry[0]];
    _formatPopup.target = self;
    _formatPopup.action = @selector(transcodeChanged:);
    _formatPopup.toolTip = @"Ask the server to transcode on the fly. "
                            "Useful on slow links; “Original” always sends the stored file. "
                            "The server must have a transcoding configured for the chosen "
                            "format — Navidrome ships MP3, Opus and AAC; FLAC and WAV have "
                            "to be added in its admin UI.";
    [root addSubview:_formatPopup];

    _bitratePopup = [[NSPopUpButton alloc] init];
    _bitratePopup.translatesAutoresizingMaskIntoConstraints = NO;
    for (NSNumber *kbps in NavidromeMaxBitrates())
        [_bitratePopup addItemWithTitle:kbps.integerValue == 0
            ? @"Unlimited"
            : [NSString stringWithFormat:@"%ld kbps", (long)kbps.integerValue]];
    _bitratePopup.target = self;
    _bitratePopup.action = @selector(transcodeChanged:);
    _bitratePopup.toolTip = @"Upper bound the server may not exceed. "
                             "Ignored when the stream isn't transcoded, and when the "
                             "target format is lossless (FLAC / WAV).";
    [root addSubview:_bitratePopup];

    _rescanButton = [NSButton buttonWithTitle:@"Rescan"
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
    NSTextField *connSection    = makeSection(@"Navidrome Server Connection");
    NSBox       *connLine       = makeLine();
    NSTextField *librarySection = makeSection(@"Rescan Navidrome Library");
    NSBox       *libraryLine    = makeLine();
    NSTextField *logsSection    = makeSection(@"Logs and Troubleshooting");
    NSBox       *logsLine       = makeLine();

    _diagButton = [NSButton buttonWithTitle:@"Copy Diagnostics"
                                     target:self
                                     action:@selector(copyDiagnostics:)];
    _diagButton.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:_diagButton];

    _logFolderButton = [NSButton buttonWithTitle:@"Show Log in Finder"
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
        [NSString stringWithFormat:@"%s\n%s", navidrome::kPrefsAuthorLine, navidrome::kSourceCodeUrl]];
    creditLabel.translatesAutoresizingMaskIntoConstraints = NO;
    creditLabel.textColor = [NSColor tertiaryLabelColor];
    creditLabel.font = [NSFont systemFontOfSize:10];
    creditLabel.selectable = YES;
    [root addSubview:creditLabel];

    CGFloat pad   = 16;
    CGFloat vGap  = 10;
    CGFloat labelW = 90;
    CGFloat fieldH = 22;

    [NSLayoutConstraint activateConstraints:@[
        [connSection.topAnchor constraintEqualToAnchor:root.topAnchor constant:pad],
        [connSection.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:pad],
        [connLine.centerYAnchor constraintEqualToAnchor:connSection.centerYAnchor],
        [connLine.leadingAnchor constraintEqualToAnchor:connSection.trailingAnchor constant:8],
        [connLine.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-pad],

        [lServer.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:pad],
        [lServer.widthAnchor constraintEqualToConstant:labelW],
        [lServer.centerYAnchor constraintEqualToAnchor:_serverField.centerYAnchor],

        [_serverField.topAnchor constraintEqualToAnchor:connSection.bottomAnchor constant:vGap],
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

        [librarySection.topAnchor constraintEqualToAnchor:_bitratePopup.bottomAnchor constant:vGap * 2],
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

    self.view = root;
}

- (void)viewDidLoad {
    [super viewDidLoad];
    [self loadSettings];
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
    _statusLabel.stringValue = @"Testing…";
    _statusLabel.textColor = [NSColor secondaryLabelColor];

    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        NSError *err = nil;
        BOOL ok = [SubsonicClient.sharedClient pingWithError:&err];
        dispatch_async(dispatch_get_main_queue(), ^{
            self->_testButton.enabled = YES;
            if (ok) {
                self->_statusLabel.stringValue = @"Connected!";
                self->_statusLabel.textColor = [NSColor systemGreenColor];
            } else {
                self->_statusLabel.stringValue = [NSString stringWithFormat:@"%@",
                    err.localizedDescription ?: @"Connection failed"];
                self->_statusLabel.textColor = [NSColor systemRedColor];
            }
        });
    });
}

- (IBAction)rescanLibrary:(id)sender {
    _rescanButton.enabled = NO;
    _scanStatusLabel.textColor = [NSColor secondaryLabelColor];
    _scanStatusLabel.stringValue = @"Starting scan…";

    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        NSError *err = nil;
        BOOL scanning = NO;
        NSInteger count = 0;
        BOOL ok = [SubsonicClient.sharedClient startScanWithScanning:&scanning
                                                                 count:&count
                                                                 error:&err];
        if (!ok) {
            dispatch_async(dispatch_get_main_queue(), ^{
                self->_rescanButton.enabled = YES;
                self->_scanStatusLabel.textColor = [NSColor systemRedColor];
                self->_scanStatusLabel.stringValue = [NSString stringWithFormat:@"Scan failed: %@",
                    err.localizedDescription ?: @"unknown error"];
            });
            return;
        }

        while (scanning) {
            [NSThread sleepForTimeInterval:navidrome::kScanPollIntervalMs / 1000.0];
            NSError *pollErr = nil;
            BOOL polled = [SubsonicClient.sharedClient getScanStatusWithScanning:&scanning
                                                                             count:&count
                                                                             error:&pollErr];
            if (!polled) break;
            dispatch_async(dispatch_get_main_queue(), ^{
                self->_scanStatusLabel.stringValue = [NSString stringWithFormat:@"Scanning… %ld processed",
                    (long)count];
            });
        }

        dispatch_async(dispatch_get_main_queue(), ^{
            self->_rescanButton.enabled = YES;
            self->_scanStatusLabel.textColor = [NSColor systemGreenColor];
            self->_scanStatusLabel.stringValue = [NSString stringWithFormat:@"Scan complete — %ld items",
                (long)count];
        });
    });
}

- (IBAction)copyDiagnostics:(id)sender {
    _diagButton.enabled = NO;
    _diagStatusLabel.stringValue = @"Collecting…";
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
            self->_diagStatusLabel.stringValue = ok ? @"Copied to the clipboard"
                                                    : @"Couldn't copy the diagnostics";
        });
    });
}

- (IBAction)showLogInFinder:(id)sender {
    const std::string path = navidrome::componentLogPath();
    if (path.empty()) { _diagStatusLabel.stringValue = @"No log file yet"; return; }
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
        @"AudioMuse-AI analyses your Navidrome library for Text Search, Instant Playlist and "
         "Song Alchemy (File › AudioMuse-AI, and the Navidrome submenu on tracks)."];
    intro.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:intro];

    _urlField    = [NSTextField textFieldWithString:@""];
    _urlField.placeholderString = @"http://audiomuse:8000";
    _tokenField  = [[NSSecureTextField alloc] init];
    _tokenField.placeholderString = @"only if AudioMuse-AI has auth enabled";
    _serverField = [NSTextField textFieldWithString:@""];
    _serverField.placeholderString = @"only if it serves several media servers";
    _countField  = [NSTextField textFieldWithString:@""];
    NSNumberFormatter *nf = [NSNumberFormatter new];
    nf.minimum = @1; nf.maximum = @(navidrome::audiomuse::kMaxCount); nf.allowsFloats = NO;
    _countField.formatter = nf;

    NSGridView *grid = [NSGridView gridViewWithViews:@[
        @[[NSTextField labelWithString:@"Server URL:"],  _urlField],
        @[[NSTextField labelWithString:@"API token:"],   _tokenField],
        @[[NSTextField labelWithString:@"Server name:"], _serverField],
        @[[NSTextField labelWithString:@"Tracks:"],      _countField],
    ]];
    grid.translatesAutoresizingMaskIntoConstraints = NO;
    [grid columnAtIndex:0].xPlacement = NSGridCellPlacementTrailing;
    grid.rowAlignment = NSGridRowAlignmentFirstBaseline;
    grid.columnSpacing = 8;
    grid.rowSpacing = 8;
    [root addSubview:grid];

    NSTextField *help = [NSTextField wrappingLabelWithString:
        @"“Tracks” also sizes Instant Mix, which needs no AudioMuse-AI settings: Navidrome "
         "answers it itself (from the AudioMuse-AI plugin when installed)."];
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
