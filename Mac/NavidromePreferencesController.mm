#import "NavidromePreferencesController.h"
#import "../SubsonicClient.h"
#include <SDK/cfg_var.h>

// Forward declarations of config vars defined in NavidromePlugin.mm
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

// Streaming transcode options. The stored value is what goes on the wire as
// stream.view's `format` — "" leaves the decision to the server's own
// transcoding rules, "raw" forces the original file.
//
// The server can only honour a format it has a transcoding configured for.
// Navidrome ships mp3 / opus / aac; FLAC and WAV need a transcoding row added
// in its admin UI first, and are mainly useful as lossless normalisation
// targets for source codecs foobar2000 can't decode itself.
static NSArray<NSArray *> *NavidromeStreamFormats(void) {
    return @[ @[@"Server default", @""],
              @[@"Original (no transcoding)", @"raw"],
              @[@"MP3", @"mp3"],
              @[@"Opus", @"opus"],
              @[@"AAC", @"aac"],
              @[@"FLAC (lossless)", @"flac"],
              @[@"WAV (uncompressed)", @"wav"] ];
}

// kbps ceiling; 0 means "no limit", which is also what Subsonic reads when the
// parameter is absent.
static NSArray<NSNumber *> *NavidromeMaxBitrates(void) {
    return @[ @0, @64, @96, @128, @192, @256, @320 ];
}

// ---------------------------------------------------------------------------
// Custom HTTP headers editor — a standalone window opened from the prefs page.
// Multiline "Name: Value" per line; persisted to cfg_custom_headers. The
// "Add Cloudflare headers" button inserts the two CF Access service-token
// header names so the user only pastes the id/secret values.
// ---------------------------------------------------------------------------
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
    // The two names don't overlap, so checking each against the original text
    // is enough to avoid duplicates on repeated clicks.
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
@property (nonatomic, strong) NSPopUpButton      *formatPopup;
@property (nonatomic, strong) NSPopUpButton      *bitratePopup;
@end

@implementation NavidromePreferencesController

- (instancetype)init {
    // No XIB — build UI programmatically in loadView
    self = [super initWithNibName:nil bundle:nil];
    return self;
}

// ---------------------------------------------------------------------------
// Programmatic view
// ---------------------------------------------------------------------------

- (void)loadView {
    NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 400, 260)];

    // Helper: make label
    auto makeLabel = ^NSTextField *(NSString *text) {
        NSTextField *lbl = [NSTextField labelWithString:text];
        lbl.translatesAutoresizingMaskIntoConstraints = NO;
        lbl.alignment = NSTextAlignmentRight;
        [root addSubview:lbl];
        return lbl;
    };

    // Helper: make text field
    auto makeField = ^NSTextField *(NSString *placeholder) {
        NSTextField *f = [[NSTextField alloc] init];
        f.translatesAutoresizingMaskIntoConstraints = NO;
        f.placeholderString = placeholder;
        [root addSubview:f];
        return f;
    };

    // Helper: make secure field
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

    // Test button
    _testButton = [NSButton buttonWithTitle:@"Test Connection"
                                     target:self
                                     action:@selector(testConnection:)];
    _testButton.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:_testButton];

    // Custom Headers button
    _headersButton = [NSButton buttonWithTitle:@"Custom Headers…"
                                        target:self
                                        action:@selector(openCustomHeaders:)];
    _headersButton.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:_headersButton];

    // Scrobbling toggle
    _scrobbleCheckbox = [NSButton checkboxWithTitle:@"Report plays to Navidrome (scrobbling)"
                                             target:self
                                             action:@selector(scrobbleToggled:)];
    _scrobbleCheckbox.translatesAutoresizingMaskIntoConstraints = NO;
    _scrobbleCheckbox.toolTip = @"Updates play counts and “Recently Played” on the "
                                 "server, and feeds any Last.fm / ListenBrainz relay it has "
                                 "configured.";
    [root addSubview:_scrobbleCheckbox];

    // Streaming transcode controls. Both are per-request stream.view params, so
    // a change takes effect on the next track without reconnecting.
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

    // Rescan button — useful if files were added/removed server-side and the
    // user doesn't want to wait for Navidrome's own scan schedule.
    _rescanButton = [NSButton buttonWithTitle:@"Rescan Library Now"
                                        target:self
                                        action:@selector(rescanLibrary:)];
    _rescanButton.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:_rescanButton];

    _scanStatusLabel = [NSTextField labelWithString:@""];
    _scanStatusLabel.translatesAutoresizingMaskIntoConstraints = NO;
    _scanStatusLabel.textColor = [NSColor secondaryLabelColor];
    _scanStatusLabel.font = [NSFont systemFontOfSize:11];
    [root addSubview:_scanStatusLabel];

    // Status label
    _statusLabel = [NSTextField labelWithString:@""];
    _statusLabel.translatesAutoresizingMaskIntoConstraints = NO;
    _statusLabel.textColor = [NSColor secondaryLabelColor];
    _statusLabel.font = [NSFont systemFontOfSize:11];
    [root addSubview:_statusLabel];

    // Info label at bottom
    NSTextField *infoLabel = [NSTextField wrappingLabelWithString:
        @"After saving, open File › Open Navidrome Browser to browse your music library."];
    infoLabel.translatesAutoresizingMaskIntoConstraints = NO;
    infoLabel.textColor = [NSColor secondaryLabelColor];
    infoLabel.font = [NSFont systemFontOfSize:11];
    [root addSubview:infoLabel];

    CGFloat pad   = 16;
    CGFloat vGap  = 10;
    CGFloat labelW = 90;
    CGFloat fieldH = 22;

    [NSLayoutConstraint activateConstraints:@[
        // Server row
        [lServer.topAnchor constraintEqualToAnchor:root.topAnchor constant:pad],
        [lServer.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:pad],
        [lServer.widthAnchor constraintEqualToConstant:labelW],
        [lServer.centerYAnchor constraintEqualToAnchor:_serverField.centerYAnchor],

        [_serverField.topAnchor constraintEqualToAnchor:root.topAnchor constant:pad],
        [_serverField.leadingAnchor constraintEqualToAnchor:lServer.trailingAnchor constant:8],
        [_serverField.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-pad],
        [_serverField.heightAnchor constraintEqualToConstant:fieldH],

        // Username row
        [lUser.topAnchor constraintEqualToAnchor:_serverField.bottomAnchor constant:vGap],
        [lUser.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:pad],
        [lUser.widthAnchor constraintEqualToConstant:labelW],
        [lUser.centerYAnchor constraintEqualToAnchor:_usernameField.centerYAnchor],

        [_usernameField.topAnchor constraintEqualToAnchor:_serverField.bottomAnchor constant:vGap],
        [_usernameField.leadingAnchor constraintEqualToAnchor:lUser.trailingAnchor constant:8],
        [_usernameField.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-pad],
        [_usernameField.heightAnchor constraintEqualToConstant:fieldH],

        // Password row
        [lPassword.topAnchor constraintEqualToAnchor:_usernameField.bottomAnchor constant:vGap],
        [lPassword.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:pad],
        [lPassword.widthAnchor constraintEqualToConstant:labelW],
        [lPassword.centerYAnchor constraintEqualToAnchor:_passwordField.centerYAnchor],

        [_passwordField.topAnchor constraintEqualToAnchor:_usernameField.bottomAnchor constant:vGap],
        [_passwordField.leadingAnchor constraintEqualToAnchor:lPassword.trailingAnchor constant:8],
        [_passwordField.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-pad],
        [_passwordField.heightAnchor constraintEqualToConstant:fieldH],

        // Test button + status
        [_testButton.topAnchor constraintEqualToAnchor:_passwordField.bottomAnchor constant:vGap * 1.5],
        [_testButton.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:pad + labelW + 8],

        [_statusLabel.centerYAnchor constraintEqualToAnchor:_testButton.centerYAnchor],
        [_statusLabel.leadingAnchor constraintEqualToAnchor:_testButton.trailingAnchor constant:10],
        [_statusLabel.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-pad],

        // Custom Headers button (below Test row)
        [_headersButton.topAnchor constraintEqualToAnchor:_testButton.bottomAnchor constant:vGap],
        [_headersButton.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:pad + labelW + 8],

        // Scrobbling checkbox
        [_scrobbleCheckbox.topAnchor constraintEqualToAnchor:_headersButton.bottomAnchor constant:vGap],
        [_scrobbleCheckbox.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:pad + labelW + 8],

        // Stream format row
        [lFormat.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:pad],
        [lFormat.widthAnchor constraintEqualToConstant:labelW],
        [lFormat.centerYAnchor constraintEqualToAnchor:_formatPopup.centerYAnchor],

        [_formatPopup.topAnchor constraintEqualToAnchor:_scrobbleCheckbox.bottomAnchor constant:vGap],
        [_formatPopup.leadingAnchor constraintEqualToAnchor:lFormat.trailingAnchor constant:8],

        // Max bitrate row
        [lBitrate.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:pad],
        [lBitrate.widthAnchor constraintEqualToConstant:labelW],
        [lBitrate.centerYAnchor constraintEqualToAnchor:_bitratePopup.centerYAnchor],

        [_bitratePopup.topAnchor constraintEqualToAnchor:_formatPopup.bottomAnchor constant:vGap],
        [_bitratePopup.leadingAnchor constraintEqualToAnchor:lBitrate.trailingAnchor constant:8],

        // Rescan button + status
        [_rescanButton.topAnchor constraintEqualToAnchor:_bitratePopup.bottomAnchor constant:vGap * 1.5],
        [_rescanButton.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:pad + labelW + 8],

        [_scanStatusLabel.centerYAnchor constraintEqualToAnchor:_rescanButton.centerYAnchor],
        [_scanStatusLabel.leadingAnchor constraintEqualToAnchor:_rescanButton.trailingAnchor constant:10],
        [_scanStatusLabel.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-pad],

        // Info label
        [infoLabel.topAnchor constraintEqualToAnchor:_rescanButton.bottomAnchor constant:vGap * 2],
        [infoLabel.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:pad],
        [infoLabel.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-pad],
    ]];

    // Set notifications for immediate-save behaviour (foobar2000 preferences pages
    // are expected to apply changes as they're made, not on an "Apply" button).
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

// ---------------------------------------------------------------------------
// Load / save
// ---------------------------------------------------------------------------

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
}

// ---------------------------------------------------------------------------
// Test connection
// ---------------------------------------------------------------------------

- (IBAction)testConnection:(id)sender {
    [self saveSettings];
    _testButton.enabled = NO;
    _statusLabel.stringValue = @"Testing…";
    _statusLabel.textColor = [NSColor secondaryLabelColor];

    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        NSError *err = nil;
        BOOL ok = [SubsonicClient.sharedClient pingWithError:&err];
        dispatch_async(dispatch_get_main_queue(), ^{
            _testButton.enabled = YES;
            if (ok) {
                _statusLabel.stringValue = @"Connected!";
                _statusLabel.textColor = [NSColor systemGreenColor];
            } else {
                _statusLabel.stringValue = [NSString stringWithFormat:@"%@",
                    err.localizedDescription ?: @"Connection failed"];
                _statusLabel.textColor = [NSColor systemRedColor];
            }
        });
    });
}

// Kicks off a server-side rescan and polls getScanStatus.view until it
// finishes. Subsonic doesn't report a total item count up front, so the
// status text can only show "N processed", not a percentage.
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
                _rescanButton.enabled = YES;
                _scanStatusLabel.textColor = [NSColor systemRedColor];
                _scanStatusLabel.stringValue = [NSString stringWithFormat:@"Scan failed: %@",
                    err.localizedDescription ?: @"unknown error"];
            });
            return;
        }

        while (scanning) {
            [NSThread sleepForTimeInterval:1.5];
            NSError *pollErr = nil;
            BOOL polled = [SubsonicClient.sharedClient getScanStatusWithScanning:&scanning
                                                                             count:&count
                                                                             error:&pollErr];
            if (!polled) break;   // transient error — stop polling, last known count stands
            dispatch_async(dispatch_get_main_queue(), ^{
                _scanStatusLabel.stringValue = [NSString stringWithFormat:@"Scanning… %ld processed",
                    (long)count];
            });
        }

        dispatch_async(dispatch_get_main_queue(), ^{
            _rescanButton.enabled = YES;
            _scanStatusLabel.textColor = [NSColor systemGreenColor];
            _scanStatusLabel.stringValue = [NSString stringWithFormat:@"Scan complete — %ld items",
                (long)count];
        });
    });
}

- (IBAction)openCustomHeaders:(id)sender {
    [NavidromeHeadersEditor show];
}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
}

@end
