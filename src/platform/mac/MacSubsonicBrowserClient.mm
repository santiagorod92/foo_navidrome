#import "MacSubsonicBrowserClient.h"
#import "SubsonicClient.h"
#import "NavidromeBrowserController.h"
#include "../../core/NavidromeLibraryPlatform.h"
#include "../../core/CoreBrowserClient.h"
#include "../../core/NavidromeDebugLog.h"
#include "../../core/NavidromeSettings.h"
#import <Foundation/Foundation.h>
#include <string>
#include <utility>
#include <vector>

namespace {

std::string errString(NSError *err) {
    if (!err) return std::string();
    NSString *d = err.localizedDescription;
    return d ? std::string(d.UTF8String) : std::string("Unknown error");
}

navidrome::CategoryKindList hiddenCategories() {
    return navidrome::parseHiddenCategories(navidrome::cfg_browser_hidden_categories.get().c_str());
}
}

std::unique_ptr<navidrome::IBrowserClient> navidrome::makeMacBrowserClient() {
    return std::make_unique<CoreBrowserClient>([SubsonicClient.sharedClient core], hiddenCategories);
}

navidrome::SubsonicCore& navidrome::libraryCore() { return [SubsonicClient.sharedClient core]; }

std::string navidrome::systemLanguageCode() {
    @autoreleasepool {
        NSString *lang = NSLocale.preferredLanguages.firstObject;
        return lang ? std::string(lang.UTF8String) : std::string();
    }
}

void navidrome::serverSettingsChanged() {
    [SubsonicClient.sharedClient refreshMusicFolders];
    [NSNotificationCenter.defaultCenter postNotificationName:NavidromeBrowserSectionsDidChangeNotification
                                                      object:nil];
}

navidrome::IBrowserClient& navidrome::libraryClient() {
    static navidrome::CoreBrowserClient inst([SubsonicClient.sharedClient core], hiddenCategories);
    return inst;
}

std::vector<uint8_t> navidrome::libraryFetchCover(const std::string& id, int size, abort_callback& abort) {
    abort.check();
    @autoreleasepool {
        NSURL *url = [SubsonicClient.sharedClient coverArtURLForId:@(id.c_str()) size:size];
        if (!url) return {};
        NSError *err = nil;
        NSData *data = [SubsonicClient.sharedClient dataForURL:url error:&err];
        abort.check();
        if (!data || data.length == 0) {
            NAVIDROME_WARN("Library", "no cover for id=" + id + (err ? " (" + errString(err) + ")" : std::string()));
            return {};
        }
        const auto* p = static_cast<const uint8_t*>(data.bytes);
        return std::vector<uint8_t>(p, p + data.length);
    }
}
