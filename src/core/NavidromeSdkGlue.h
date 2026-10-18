#pragma once

#include "NavidromeBrowserModel.h"
#include <SDK/metadb.h>
#include <functional>
#include <string>
#include <vector>

namespace navidrome {

metadb_handle_list makeTrackHandles(const std::vector<BrowserNodePtr>& nodes,
                                    const std::function<std::string(const std::string&)>& radioUrl);

void playFrom(t_size playlist, t_size first);

void reportError(const char* title, const std::string& msg);

void showConfirmation(const char* title, const std::string& msg);

}
