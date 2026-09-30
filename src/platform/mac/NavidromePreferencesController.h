#pragma once
#import <Cocoa/Cocoa.h>

// NSViewController subclass used as the foobar2000 preferences page.
// The instance is wrapped with fb2k::wrapNSObject() and returned from
// preferences_page_navidrome::instantiate() in NavidromePlugin.mm
@interface NavidromePreferencesController : NSViewController
@end
