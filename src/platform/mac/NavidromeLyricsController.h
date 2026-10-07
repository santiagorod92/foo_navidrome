#pragma once
#import <Cocoa/Cocoa.h>

// "Navidrome Lyrics" layout panel (ui_element_mac): shows the now-playing navidrome:// track's
// lyrics, highlighting and centring the current line when the server has synced timings. macOS
// has no ESLyric, so this is the Mac's lyrics display; the lookup itself is shared
// (navidrome::lyricsForTrackURI — same cache navidrome_lyrics_api serves other components from).
// One instance per mount, like NavidromeBrowserController.
@interface NavidromeLyricsController : NSViewController
@end
