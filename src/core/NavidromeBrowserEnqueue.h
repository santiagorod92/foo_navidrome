#pragma once

#include "NavidromeBrowserModel.h"

#include <functional>
#include <string>
#include <vector>

namespace navidrome {

std::size_t enqueueBrowserNodes(
    const std::vector<BrowserNodePtr>& nodes,
    bool play,
    bool clearFirst,
    const std::function<std::string(const std::string& radioId)>& radioUrl,
    std::string& statusOut);

void showBrowserQueueError(const std::string& msg);

std::size_t playNodesInNewPlaylist(const std::vector<BrowserNodePtr>& nodes,
                                   const std::string& name);

void seekWhenReady(double positionSeconds);
}
