#import "NavidromeLyricsController.h"
#include "../../core/stdafx.h"
#include "../../core/SubsonicTypes.h"
#include "../../core/NavidromeBrowserModel.h"
#include "../../core/NavidromeLibraryPlatform.h"
#include "../../core/NavidromeDebugLog.h"
#include <SDK/play_callback.h>
#include <SDK/playback_control.h>
#include <memory>
#include <string>
#include <vector>

@interface NavidromeLyricsController ()
- (void)trackChanged:(metadb_handle_ptr)track;
- (void)playbackStopped;
- (void)tick;
@end

namespace {

// How often the synced-line highlight follows the playback position. on_playback_time is only
// once a second — too coarse for line changes — so a timer polls playback_get_position().
constexpr NSTimeInterval kTickSeconds = 0.2;

// Registered while the panel is on screen; all callbacks arrive on the main thread.
class LyricsPlayCallback : public play_callback_impl_base {
public:
    explicit LyricsPlayCallback(NavidromeLyricsController *owner)
        : play_callback_impl_base(flag_on_playback_new_track | flag_on_playback_stop |
                                  flag_on_playback_seek),
          m_owner(owner) {}

    void on_playback_new_track(metadb_handle_ptr track) override { [m_owner trackChanged:track]; }
    void on_playback_stop(play_control::t_stop_reason reason) override {
        if (reason != play_control::stop_reason_starting_another) [m_owner playbackStopped];
    }
    void on_playback_seek(double) override { [m_owner tick]; }

private:
    __weak NavidromeLyricsController *m_owner;
};

} // namespace

@implementation NavidromeLyricsController {
    NSScrollView *_scroll;
    NSTextView *_textView;
    NSTimer *_timer;
    std::unique_ptr<LyricsPlayCallback> _callback;

    std::string _uri;              // track currently shown ("" = none)
    navidrome::Lyrics _lyrics;
    std::vector<NSRange> _lineRanges;
    int _activeLine;
    NSUInteger _generation;        // bumps per track change; stale fetches are dropped
}

- (void)loadView {
    NSView *content = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 360, 480)];
    self.view = content;

    _scroll = [[NSScrollView alloc] initWithFrame:content.bounds];
    _scroll.translatesAutoresizingMaskIntoConstraints = NO;
    _scroll.hasVerticalScroller = YES;
    _scroll.autohidesScrollers = YES;
    _scroll.drawsBackground = NO;
    _scroll.borderType = NSNoBorder;

    _textView = [[NSTextView alloc] initWithFrame:_scroll.contentView.bounds];
    _textView.editable = NO;
    _textView.selectable = YES;
    _textView.drawsBackground = NO;
    _textView.textContainerInset = NSMakeSize(12, 16);
    _textView.verticallyResizable = YES;
    _textView.horizontallyResizable = NO;
    _textView.autoresizingMask = NSViewWidthSizable;
    _textView.textContainer.widthTracksTextView = YES;
    _scroll.documentView = _textView;
    [content addSubview:_scroll];

    [NSLayoutConstraint activateConstraints:@[
        [_scroll.topAnchor constraintEqualToAnchor:content.topAnchor],
        [_scroll.bottomAnchor constraintEqualToAnchor:content.bottomAnchor],
        [_scroll.leadingAnchor constraintEqualToAnchor:content.leadingAnchor],
        [_scroll.trailingAnchor constraintEqualToAnchor:content.trailingAnchor],
    ]];
    _activeLine = -1;
    [self showMessage:@"Nothing playing"];
}

- (void)viewDidAppear {
    [super viewDidAppear];
    if (!_callback) _callback = std::make_unique<LyricsPlayCallback>(self);
    if (!_timer) {
        __weak NavidromeLyricsController *weakSelf = self;
        _timer = [NSTimer scheduledTimerWithTimeInterval:kTickSeconds repeats:YES
                                                   block:^(NSTimer *) { [weakSelf tick]; }];
    }
    metadb_handle_ptr track;
    if (playback_control::get()->get_now_playing(track)) [self trackChanged:track];
    else [self playbackStopped];
}

- (void)viewDidDisappear {
    [super viewDidDisappear];
    _callback.reset();
    [_timer invalidate];
    _timer = nil;
}

- (void)dealloc {
    [_timer invalidate];
}

// --- Playback -------------------------------------------------------------------------------

- (void)trackChanged:(metadb_handle_ptr)track {
    const std::string uri = track.is_valid() ? std::string(track->get_path()) : std::string();
    if (uri == _uri) return;
    _uri = uri;
    const NSUInteger gen = ++_generation;
    _lyrics = {};

    if (navidrome::trackIdFromURI(uri).empty()) {
        [self showMessage:@"Lyrics are shown for Navidrome tracks"];
        return;
    }
    if (!navidrome::libraryIsConfigured()) {
        [self showMessage:@"Navidrome server not configured"];
        return;
    }
    [self showMessage:@"Loading lyrics…"];

    __weak NavidromeLyricsController *weakSelf = self;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        navidrome::Lyrics lyrics;
        std::string err;
        navidrome::dbg::runGuarded("Lyrics", "lyrics fetch", [&] {
            lyrics = navidrome::lyricsForTrackURI(navidrome::libraryClient(), uri, err);
        });
        if (!err.empty()) NAVIDROME_WARN("Lyrics", "panel fetch failed: " + err);
        dispatch_async(dispatch_get_main_queue(), ^{
            NavidromeLyricsController *strongSelf = weakSelf;
            if (!strongSelf || strongSelf->_generation != gen) return;
            [strongSelf showLyrics:lyrics error:err];
        });
    });
}

- (void)playbackStopped {
    _uri.clear();
    ++_generation;
    _lyrics = {};
    [self showMessage:@"Nothing playing"];
}

- (void)tick {
    if (!_lyrics.synced || _lineRanges.empty()) return;
    const double pos = playback_control::get()->playback_get_position();
    [self highlightLine:navidrome::activeLyricLine(_lyrics, (long long)(pos * 1000.0))];
}

// --- Rendering ------------------------------------------------------------------------------

- (NSDictionary *)attributesActive:(BOOL)active {
    NSMutableParagraphStyle *para = [NSMutableParagraphStyle new];
    para.alignment = NSTextAlignmentCenter;
    para.paragraphSpacing = 6;
    const CGFloat size = [NSFont systemFontSize] + 1;
    return @{
        NSFontAttributeName: active ? [NSFont boldSystemFontOfSize:size] : [NSFont systemFontOfSize:size],
        NSForegroundColorAttributeName: active ? [NSColor labelColor] : [NSColor secondaryLabelColor],
        NSParagraphStyleAttributeName: para,
    };
}

- (void)showMessage:(NSString *)message {
    _lineRanges.clear();
    _activeLine = -1;
    NSMutableDictionary *attrs = [[self attributesActive:NO] mutableCopy];
    attrs[NSForegroundColorAttributeName] = [NSColor tertiaryLabelColor];
    [_textView.textStorage setAttributedString:
        [[NSAttributedString alloc] initWithString:message attributes:attrs]];
    [_textView scrollPoint:NSZeroPoint];
}

- (void)showLyrics:(const navidrome::Lyrics &)lyrics error:(const std::string &)err {
    _lyrics = lyrics;
    if (!err.empty()) { [self showMessage:@"Couldn't load lyrics (see console log)"]; return; }
    if (lyrics.empty()) { [self showMessage:@"No lyrics on the server for this track"]; return; }

    // Unsynced lyrics read as plain text, so they get the full label colour; synced ones start
    // dimmed and the current line is lit by -highlightLine:.
    NSMutableDictionary *base = [[self attributesActive:NO] mutableCopy];
    if (!lyrics.synced) base[NSForegroundColorAttributeName] = [NSColor labelColor];
    NSMutableAttributedString *text = [NSMutableAttributedString new];
    _lineRanges.clear();
    for (size_t i = 0; i < lyrics.lines.size(); ++i) {
        NSString *line = @(lyrics.lines[i].text.c_str()) ?: @"";
        if (i + 1 < lyrics.lines.size()) line = [line stringByAppendingString:@"\n"];
        _lineRanges.push_back(NSMakeRange(text.length, line.length));
        [text appendAttributedString:[[NSAttributedString alloc] initWithString:line attributes:base]];
    }
    _activeLine = -1;
    [_textView.textStorage setAttributedString:text];
    [_textView scrollPoint:NSZeroPoint];
    [self tick];
}

- (void)highlightLine:(int)index {
    if (index == _activeLine) return;
    NSTextStorage *storage = _textView.textStorage;
    [storage beginEditing];
    if (_activeLine >= 0 && (size_t)_activeLine < _lineRanges.size())
        [storage setAttributes:[self attributesActive:NO] range:_lineRanges[_activeLine]];
    _activeLine = index;
    if (index >= 0 && (size_t)index < _lineRanges.size())
        [storage setAttributes:[self attributesActive:YES] range:_lineRanges[index]];
    [storage endEditing];
    if (index >= 0 && (size_t)index < _lineRanges.size()) [self centerRange:_lineRanges[index]];
}

- (void)centerRange:(NSRange)range {
    NSLayoutManager *lm = _textView.layoutManager;
    NSRange glyphs = [lm glyphRangeForCharacterRange:range actualCharacterRange:NULL];
    NSRect rect = [lm boundingRectForGlyphRange:glyphs inTextContainer:_textView.textContainer];
    NSClipView *clip = _scroll.contentView;
    const CGFloat visible = NSHeight(clip.bounds);
    CGFloat y = NSMidY(rect) + _textView.textContainerOrigin.y - visible / 2;
    y = MAX(0, MIN(y, NSHeight(_textView.frame) - visible));
    [NSAnimationContext runAnimationGroup:^(NSAnimationContext *ctx) {
        ctx.duration = 0.25;
        [clip.animator setBoundsOrigin:NSMakePoint(0, y)];
    } completionHandler:nil];
    [_scroll reflectScrolledClipView:clip];
}

@end
