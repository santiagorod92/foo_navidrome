#pragma once
#import <Cocoa/Cocoa.h>

// NSViewController subclass used as the foobar2000 preferences page.
// The instance is wrapped with fb2k::wrapNSObject() and returned from
// preferences_page_navidrome::instantiate() in NavidromePlugin.mm
@interface NavidromePreferencesController : NSViewController
@end

// Preferences > Tools > Navidrome > AudioMuse-AI (issue #16) — server URL, API
// token, server name and track count, written live like every Mac prefs page.
// Registered in NavidromePlugin.mm.
@interface NavidromeAudioMusePrefsController : NSViewController
@end
