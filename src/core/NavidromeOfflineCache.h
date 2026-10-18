#pragma once

#include "NavidromeBrowserModel.h"
#include "OfflineCache.h"
#include "TrackUri.h"

#include <string>
#include <vector>

namespace navidrome {

offline::Store& offlineStore();

std::string offlineCachedPath(const TrackURI& track);

bool          prefetchEnabled();
std::uint64_t prefetchLimitMB();

void makeAvailableOffline(std::vector<offline::Item> items);
void removeOfflineCopies(const std::vector<std::string>& ids);

void makeNodesAvailableOffline(const std::vector<BrowserNodePtr>& songs);
void removeNodesOfflineCopies(const std::vector<BrowserNodePtr>& songs);

}
