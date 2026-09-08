#pragma once
// Adapts the macOS ObjC SubsonicClient singleton to the platform-neutral
// navidrome::IBrowserClient seam that the shared browser tree logic
// (NavidromeBrowserModel.h: buildRootNodes / fetchChildren / collectSongsDeep)
// is written against. Converts NSArray<SubsonicX *> -> std::vector<navidrome::X>
// and NSError * -> a human-readable error string. ObjC++ only.

#include "../NavidromeBrowserModel.h"
#include <memory>

namespace navidrome {

// Returns an IBrowserClient backed by [SubsonicClient sharedClient]. Cheap to
// create per call; holds no state of its own.
std::unique_ptr<IBrowserClient> makeMacBrowserClient();

} // namespace navidrome
