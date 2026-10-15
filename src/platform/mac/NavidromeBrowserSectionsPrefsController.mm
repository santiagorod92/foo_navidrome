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

@interface NavidromeBrowserSectionsPrefsController : NSViewController
@end

@implementation NavidromeBrowserSectionsPrefsController {
    NSMutableArray<NSButton *> *_boxes;
}

- (instancetype)init {
    self = [super initWithNibName:nil bundle:nil];
    if (self) _boxes = [NSMutableArray array];
    return self;
}

- (void)loadView {
    NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 480, 400)];

    NSTextField *heading = [NSTextField labelWithString:@"Show these sections in the browser tree:"];

    NSStackView *stack = [NSStackView stackViewWithViews:@[ heading ]];
    stack.orientation = NSUserInterfaceLayoutOrientationVertical;
    stack.alignment = NSLayoutAttributeLeading;
    stack.spacing = 6;
    stack.translatesAutoresizingMaskIntoConstraints = NO;

    const auto hidden =
        navidrome::parseHiddenCategories(navidrome::cfg_browser_hidden_categories.get().c_str());
    NSInteger tag = 0;
    for (const auto &c : navidrome::browserCategories()) {
        NSButton *box = [NSButton checkboxWithTitle:@(c.title)
                                             target:self
                                             action:@selector(sectionToggled:)];
        box.tag = tag++;
        box.state = navidrome::containsCategory(hidden, c.kind)
            ? NSControlStateValueOff : NSControlStateValueOn;
        [_boxes addObject:box];
        [stack addView:box inGravity:NSStackViewGravityTop];
        [stack setCustomSpacing:4 afterView:box];
    }
    [stack setCustomSpacing:10 afterView:heading];

    NSTextField *note = [NSTextField labelWithString:
        @"Artists (and libraries, on a multi-library server) are always shown."];
    note.textColor = [NSColor secondaryLabelColor];
    note.font = [NSFont systemFontOfSize:11];
    [stack addView:note inGravity:NSStackViewGravityTop];
    [stack setCustomSpacing:12 afterView:_boxes.lastObject];

    [root addSubview:stack];
    CGFloat pad = 16;
    [NSLayoutConstraint activateConstraints:@[
        [stack.topAnchor constraintEqualToAnchor:root.topAnchor constant:pad],
        [stack.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:pad],
        [stack.trailingAnchor constraintLessThanOrEqualToAnchor:root.trailingAnchor constant:-pad],
    ]];
    self.view = root;
}

- (IBAction)sectionToggled:(NSButton *)sender {
    navidrome::CategoryKindList hidden;
    const auto &cats = navidrome::browserCategories();
    for (NSButton *box in _boxes) {
        NSInteger i = box.tag;
        if (i >= 0 && i < (NSInteger)cats.size() && box.state != NSControlStateValueOn)
            hidden.push_back(cats[(size_t)i].kind);
    }
    const std::string csv = navidrome::joinHiddenCategories(hidden);
    navidrome::cfg_browser_hidden_categories.set(csv.c_str());
    NAVIDROME_LOG("UI", "browser sections: hidden = [" + csv + "]");
    [NSNotificationCenter.defaultCenter
        postNotificationName:NavidromeBrowserSectionsDidChangeNotification object:nil];
}

@end

namespace {

class preferences_page_navidrome_sections : public preferences_page {
public:
    service_ptr instantiate() override {
        return fb2k::wrapNSObject([NavidromeBrowserSectionsPrefsController new]);
    }
    const char *get_name() override { return "Browser Sections"; }
    GUID get_guid() override { return navidrome::guids::sectionsPrefsPage; }
    GUID get_parent_guid() override { return navidrome::guids::prefsPage; }
};

FB2K_SERVICE_FACTORY(preferences_page_navidrome_sections);
}
